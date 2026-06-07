#include "airport_rpc.hpp"
#include "airport_extension.hpp"
#include "airport_macros.hpp"
#include "airport_interrupt.hpp"
#include <arrow/flight/client.h>
#include <arrow/flight/types.h>
#include <arrow/buffer.h>
#include <chrono>
#include <thread>
#include <random>
#include <algorithm>

namespace duckdb
{

  std::unique_ptr<arrow::flight::Result> AirportCallAction(
      std::shared_ptr<arrow::flight::FlightClient> flight_client,
      arrow::flight::FlightCallOptions &call_options,
      const arrow::flight::Action &action,
      const std::string &server_location,
      bool want_result,
      ClientContext *context)
  {
    // Check interrupt before starting if context is available
    if (context)
    {
      AirportCheckContextInterrupt(*context);
      // Set a deadline to prevent infinite blocking (5 minute timeout)
      AirportSetCallDeadline(call_options, 300);
    }
    std::random_device rd;
    std::mt19937 gen(rd());

    auto &op_name = action.type;

    int max_retries = 5;
    std::chrono::milliseconds initial_delay{100};
    std::chrono::milliseconds max_delay{5000};
    double backoff_multiplier = 2.0;
    double jitter_factor = 0.1; // 10% jitter

    std::unique_ptr<arrow::flight::Result> results_buffer = nullptr;
    std::unique_ptr<arrow::flight::ResultStream> action_results = nullptr;

    auto compute_delay = [&](int attempt)
    {
      auto delay = initial_delay;
      for (int i = 0; i < attempt; ++i)
      {
        delay = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::duration<double, std::milli>(delay.count() * backoff_multiplier));
      }
      delay = std::min(delay, max_delay);
      if (jitter_factor > 0)
      {
        std::uniform_real_distribution<double> jitter_dist(
            1.0 - jitter_factor, 1.0 + jitter_factor);
        delay = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::duration<double, std::milli>(delay.count() * jitter_dist(gen)));
      }
      return delay;
    };

    call_options.headers.emplace_back("airport-action-name", action.type);

    // Retry DoAction
    for (int attempt = 0; attempt <= max_retries; ++attempt)
    {
      // Check interrupt before each retry attempt
      if (context)
      {
        AirportCheckContextInterrupt(*context);
      }

      arrow::Result<std::unique_ptr<arrow::flight::ResultStream>> invoke_result;

      // Use interruptible wrapper if context available, otherwise call directly
      if (context)
      {
        invoke_result = AirportInterruptibleRPC<std::unique_ptr<arrow::flight::ResultStream>>(
            *context,
            call_options,
            [&]() { return flight_client->DoAction(call_options, action); });
      }
      else
      {
        invoke_result = flight_client->DoAction(call_options, action);
      }

      if (invoke_result.ok())
      {
        action_results = std::move(invoke_result).ValueUnsafe();
        break;
      }

      // Check if the error was due to interrupt
      if (context)
      {
        AirportCheckCancelledStatus(*context, invoke_result.status());
      }

      if (attempt == max_retries || !invoke_result.status().IsIOError())
      {
        throw AirportFlightException(server_location, invoke_result.status(),
                                     (op_name + ": invoke"), {});
      }

      std::this_thread::sleep_for(compute_delay(attempt));
    }

    if (want_result)
    {
      // Retry action_results->Next()
      for (int attempt = 0; attempt <= max_retries; ++attempt)
      {
        // Check interrupt before each retry attempt
        if (context)
        {
          AirportCheckContextInterrupt(*context);
        }

        arrow::Result<std::unique_ptr<arrow::flight::Result>> next_result;

        // Use interruptible wrapper if context available
        if (context)
        {
          next_result = AirportInterruptibleRPC<std::unique_ptr<arrow::flight::Result>>(
              *context,
              call_options,
              [&]() { return action_results->Next(); });
        }
        else
        {
          next_result = action_results->Next();
        }

        if (next_result.ok())
        {
          results_buffer = std::move(next_result).ValueUnsafe();
          break;
        }

        // Check if the error was due to interrupt
        if (context)
        {
          AirportCheckCancelledStatus(*context, next_result.status());
        }

        if (attempt == max_retries || !next_result.status().IsIOError())
        {
          throw AirportFlightException(server_location, next_result.status(),
                                       (op_name + ": reading result"), {});
        }

        std::this_thread::sleep_for(compute_delay(attempt));
      }
    }

    // Check interrupt before draining results
    if (context)
    {
      AirportCheckContextInterrupt(*context);
    }

    AIRPORT_ARROW_ASSERT_OK_LOCATION(action_results->Drain(),
                                     server_location,
                                     (op_name + ": drain action result stream"));

    return results_buffer;
  }

}