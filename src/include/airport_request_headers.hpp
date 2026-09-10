#pragma once

#include <vector>
#include <string>
#include "arrow/flight/client.h"
#include "airport_flight_stream.hpp"

namespace duckdb
{
  class ClientContext;

  const std::string airport_user_agent() noexcept;
  void airport_add_standard_headers(arrow::flight::FlightCallOptions &options, const std::string &server_location, ClientContext &context) noexcept;
  void airport_add_authorization_header(arrow::flight::FlightCallOptions &options, const std::string &auth_token) noexcept;
  void airport_add_flight_path_header(arrow::flight::FlightCallOptions &options,
                                      const arrow::flight::FlightDescriptor &descriptor);
  void airport_add_trace_id_header(arrow::flight::FlightCallOptions &options,
                                   const string &trace_id);
  void airport_add_catalog_header(arrow::flight::FlightCallOptions &options,
                                  const std::string &catalog_name) noexcept;

  void airport_add_normal_headers(arrow::flight::FlightCallOptions &options,
                                  const AirportTakeFlightParameters &params,
                                  const std::string &trace_id,
                                  ClientContext &context,
                                  const std::optional<arrow::flight::FlightDescriptor> &descriptor = std::nullopt);

  // Generate a random id that is used for request tracking.
  std::string airport_trace_id();

  // The client session id sent as `airport-client-session-id`, stored per
  // ClientContext so concurrent connections in one process do not share it.
  std::string airport_client_session_id(ClientContext &context);

  // The id most recently emitted on a request header, for callers with no
  // ClientContext (the exported airport_execution_session_id()).
  std::string airport_last_client_session_id();

  // Rotate the calling connection's client session id; its subsequent requests
  // carry the new id. Exposed to SQL as `airport_reset_client_session()`.
  std::string airport_regenerate_client_session_id(ClientContext &context);

}