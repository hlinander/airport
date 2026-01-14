#pragma once

#include "duckdb.hpp"
#include <arrow/flight/client.h>
#include <arrow/flight/types.h>

namespace duckdb
{

  /**
   * Call an action on an Arrow Flight server and return the result.
   *
   * @param flight_client The Flight client to use for the call
   * @param call_options The call options for the Flight request
   * @param action The action to execute
   * @param server_location The server location for error reporting
   * @param want_result Whether to read a result from the action
   * @param context Optional client context for interrupt handling (nullptr if not available)
   * @return A unique pointer to the action result
   */
  std::unique_ptr<arrow::flight::Result> AirportCallAction(
      std::shared_ptr<arrow::flight::FlightClient> flight_client,
      arrow::flight::FlightCallOptions &call_options,
      const arrow::flight::Action &action,
      const std::string &server_location,
      bool want_result = true,
      ClientContext *context = nullptr);

} // namespace duckdb
