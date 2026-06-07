#pragma once

#include "duckdb.hpp"
#include "duckdb/main/client_context.hpp"
#include <arrow/flight/client.h>
#include <arrow/flight/types.h>
#include <arrow/result.h>
#include <future>
#include <chrono>
#include <functional>

namespace duckdb
{

  /**
   * Check if a context is interrupted and throw InterruptException if so.
   * Used to convert gRPC cancellation errors to clean interrupt exceptions.
   */
  inline void AirportCheckContextInterrupt(ClientContext &context)
  {
    if (context.interrupted)
    {
      throw InterruptException();
    }
  }

  /**
   * Check if an Arrow status represents a cancellation due to user interrupt.
   * If interrupted, throws InterruptException. Otherwise returns false.
   */
  inline bool AirportCheckCancelledStatus(ClientContext &context, const arrow::Status &status)
  {
    if (context.interrupted &&
        (status.IsCancelled() ||
         status.IsIOError() ||
         status.code() == arrow::StatusCode::UnknownError))
    {
      throw InterruptException();
    }
    return false;
  }

  /**
   * Set a reasonable deadline on Flight call options to prevent infinite blocking.
   * This allows RPCs to timeout instead of blocking forever.
   *
   * @param call_options The call options to modify
   * @param timeout_seconds Timeout in seconds (default: 300 = 5 minutes)
   */
  inline void AirportSetCallDeadline(arrow::flight::FlightCallOptions &call_options,
                                     int timeout_seconds = 300)
  {
    auto deadline = std::chrono::system_clock::now() + std::chrono::seconds(timeout_seconds);
    call_options.timeout = arrow::flight::TimeoutDuration{std::chrono::duration_cast<std::chrono::microseconds>(
        deadline.time_since_epoch()).count()};
  }

  /**
   * Execute an Arrow Flight RPC with interrupt support.
   * Runs the RPC in an async thread and periodically checks context.interrupted.
   * If interrupted, throws InterruptException.
   *
   * Note: The underlying gRPC call cannot be cancelled mid-flight in Arrow Flight's
   * synchronous API. This function will throw InterruptException when interrupted,
   * but the RPC may continue in the background. To prevent indefinite blocking,
   * use AirportSetCallDeadline() on call_options before calling this function.
   *
   * @param context The client context for interrupt checking
   * @param rpc_func A function that performs the RPC and returns an arrow::Result<T>
   * @param check_interval_ms How often to check for interrupts (default: 100ms)
   * @return The result of the RPC if successful
   * @throws InterruptException if context.interrupted becomes true
   */
  template<typename T>
  arrow::Result<T> AirportInterruptibleRPC(
      ClientContext &context,
      std::function<arrow::Result<T>()> rpc_func,
      int check_interval_ms = 100)
  {
    // Check interrupt before starting RPC
    AirportCheckContextInterrupt(context);

    // Launch RPC in async thread
    auto future = std::async(std::launch::async, std::move(rpc_func));

    // Poll for completion while checking for interrupts
    auto check_interval = std::chrono::milliseconds(check_interval_ms);
    while (future.wait_for(check_interval) == std::future_status::timeout)
    {
      // Check if query was interrupted
      if (context.interrupted)
      {
        // Note: We can't cancel the underlying gRPC call, but we throw
        // InterruptException to stop waiting. The RPC continues in background.
        throw InterruptException();
      }
    }

    // RPC completed, get result
    try
    {
      return future.get();
    }
    catch (...)
    {
      // Check if interruption caused the exception
      AirportCheckContextInterrupt(context);
      // Re-throw if not an interrupt
      throw;
    }
  }

  /**
   * Overload for RPC functions that return unique_ptr instead of arrow::Result.
   * This is useful for DoAction which returns unique_ptr<ResultStream>.
   */
  template<typename T>
  std::unique_ptr<T> AirportInterruptibleRPCPtr(
      ClientContext &context,
      std::function<std::unique_ptr<T>()> rpc_func,
      int check_interval_ms = 100)
  {
    // Check interrupt before starting RPC
    AirportCheckContextInterrupt(context);

    // Launch RPC in async thread
    auto future = std::async(std::launch::async, std::move(rpc_func));

    // Poll for completion while checking for interrupts
    auto check_interval = std::chrono::milliseconds(check_interval_ms);
    while (future.wait_for(check_interval) == std::future_status::timeout)
    {
      // Check if query was interrupted
      if (context.interrupted)
      {
        throw InterruptException();
      }
    }

    // RPC completed, get result
    try
    {
      return future.get();
    }
    catch (...)
    {
      // Check if interruption caused the exception
      AirportCheckContextInterrupt(context);
      throw;
    }
  }

} // namespace duckdb
