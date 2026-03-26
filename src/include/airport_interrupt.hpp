#pragma once

#include "duckdb.hpp"
#include "duckdb/main/client_context.hpp"
#include <arrow/flight/client.h>
#include <arrow/flight/types.h>
#include <arrow/result.h>
#include <arrow/util/cancel.h>
#include <thread>
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
   * Check an atomic interrupted flag directly (for use in adapter layers
   * that don't have a ClientContext reference).
   */
  inline void AirportCheckInterruptedFlag(const std::atomic<bool> *interrupted)
  {
    if (interrupted && interrupted->load(std::memory_order_relaxed))
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
   * RAII monitor that watches context.interrupted and performs cancellation.
   *
   * Two modes:
   * 1. Cancel callback (for streaming RPCs like DoGet/DoExchange):
   *    Calls FlightStreamReader::Cancel() which triggers
   *    grpc::ClientContext::TryCancel() and wakes up blocked Read() calls.
   *
   * 2. StopToken (for unary RPCs via AirportInterruptibleRPC):
   *    Triggers stop_source.RequestStop(). Arrow Flight polls this token
   *    between operations for some call types.
   *
   * Usage for streaming:
   *   auto stream = flight_client->DoGet(call_options, ticket);
   *   auto monitor = make_uniq<AirportInterruptMonitor>(
   *       context, [s = stream]() { s->Cancel(); });
   *
   * Usage for unary (via AirportInterruptibleRPC):
   *   AirportInterruptMonitor monitor(context);
   *   call_options.stop_token = monitor.token();
   */
  class AirportInterruptMonitor
  {
    std::atomic<bool> done_{false};
    arrow::StopSource stop_source_;
    std::function<void()> cancel_func_;
    std::thread monitor_thread_;

    void start_monitor(ClientContext &context, int check_interval_ms)
    {
      monitor_thread_ = std::thread([this, &context, check_interval_ms]()
                                    {
        auto interval = std::chrono::milliseconds(check_interval_ms);
        while (!done_.load(std::memory_order_relaxed) &&
               !context.interrupted.load(std::memory_order_relaxed))
        {
          std::this_thread::sleep_for(interval);
        }
        if (context.interrupted.load(std::memory_order_relaxed))
        {
          stop_source_.RequestStop();
          if (cancel_func_)
          {
            cancel_func_();
          }
        } });
    }

  public:
    /// Constructor with cancel callback (for streaming RPCs).
    /// The callback should call FlightStreamReader::Cancel() or similar.
    explicit AirportInterruptMonitor(ClientContext &context,
                                     std::function<void()> cancel_func,
                                     int check_interval_ms = 10)
        : cancel_func_(std::move(cancel_func))
    {
      start_monitor(context, check_interval_ms);
    }

    /// Constructor without cancel callback (for unary RPCs using StopToken).
    explicit AirportInterruptMonitor(ClientContext &context, int check_interval_ms = 10)
    {
      start_monitor(context, check_interval_ms);
    }

    ~AirportInterruptMonitor()
    {
      done_.store(true, std::memory_order_relaxed);
      if (monitor_thread_.joinable())
      {
        monitor_thread_.join();
      }
    }

    // Non-copyable, non-movable
    AirportInterruptMonitor(const AirportInterruptMonitor &) = delete;
    AirportInterruptMonitor &operator=(const AirportInterruptMonitor &) = delete;

    arrow::StopToken token()
    {
      return stop_source_.token();
    }
  };

  /**
   * Execute a short-lived Arrow Flight RPC with StopToken-based cancellation.
   *
   * Suitable for unary-style RPCs (GetFlightInfo, ListFlights, DoAction)
   * where Arrow Flight polls the stop_token between operations.
   *
   * NOT suitable for streaming RPCs (DoGet, DoExchange) — those need
   * AirportInterruptMonitor with a Cancel callback to call
   * FlightStreamReader::Cancel() directly.
   *
   * @param context The client context for interrupt checking
   * @param call_options Flight call options — stop_token will be set
   * @param rpc_func A function that performs the RPC and returns an arrow::Result<T>
   * @param check_interval_ms How often to check for interrupts (default: 10ms)
   * @return The result of the RPC if successful
   * @throws InterruptException if context.interrupted becomes true
   */
  template <typename T>
  arrow::Result<T> AirportInterruptibleRPC(
      ClientContext &context,
      arrow::flight::FlightCallOptions &call_options,
      std::function<arrow::Result<T>()> rpc_func,
      int check_interval_ms = 10)
  {
    // Check interrupt before starting RPC
    AirportCheckContextInterrupt(context);

    // Set up real cancellation via StopToken
    AirportInterruptMonitor monitor(context, check_interval_ms);
    call_options.stop_token = monitor.token();

    // Run RPC on this thread (no async for the RPC itself)
    try
    {
      return rpc_func();
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
  template <typename T>
  std::unique_ptr<T> AirportInterruptibleRPCPtr(
      ClientContext &context,
      arrow::flight::FlightCallOptions &call_options,
      std::function<std::unique_ptr<T>()> rpc_func,
      int check_interval_ms = 10)
  {
    // Check interrupt before starting RPC
    AirportCheckContextInterrupt(context);

    // Set up real cancellation via StopToken
    AirportInterruptMonitor monitor(context, check_interval_ms);
    call_options.stop_token = monitor.token();

    // Run RPC on this thread
    try
    {
      return rpc_func();
    }
    catch (...)
    {
      // Check if interruption caused the exception
      AirportCheckContextInterrupt(context);
      throw;
    }
  }

} // namespace duckdb
