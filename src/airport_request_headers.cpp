#include "airport_request_headers.hpp"
#include <string.h>
#include "duckdb/common/types/uuid.hpp"
#include <mutex>
#include <numeric>

// Indicate the version of the caller.
#define AIRPORT_USER_AGENT "airport/20250723"

namespace duckdb
{

  string airport_trace_id()
  {
    return UUID::ToString(UUID::GenerateRandomUUID());
  }

  const std::string airport_user_agent() noexcept
  {
    return AIRPORT_USER_AGENT;
  }

  // Generate a random session id for each time that DuckDB starts,
  // this can be useful on the server side for tracking sessions.
  // Mutable behind a mutex: servers that bind authorization state to the
  // session id (e.g. a workspace-scoped session) need the embedding
  // application to be able to start a fresh session when its auth context
  // changes — airport_regenerate_client_session_id() rotates the id for all
  // subsequent requests.
  static std::mutex airport_session_id_mutex;
  static std::string airport_session_id = UUID::ToString(UUID::GenerateRandomUUID());

  std::string airport_client_session_id()
  {
    std::lock_guard<std::mutex> guard(airport_session_id_mutex);
    return airport_session_id;
  }

  std::string airport_regenerate_client_session_id()
  {
    std::lock_guard<std::mutex> guard(airport_session_id_mutex);
    airport_session_id = UUID::ToString(UUID::GenerateRandomUUID());
    return airport_session_id;
  }

  static void
  airport_add_headers(std::vector<std::pair<std::string, std::string>> &headers, const std::string &server_location) noexcept
  {
    headers.emplace_back("airport-user-agent", AIRPORT_USER_AGENT);
    headers.emplace_back("authority", server_location);
    headers.emplace_back("airport-client-session-id", airport_client_session_id());
  }

  void airport_add_standard_headers(arrow::flight::FlightCallOptions &options, const std::string &server_location) noexcept
  {
    airport_add_headers(options.headers, server_location);
  }

  void airport_add_authorization_header(arrow::flight::FlightCallOptions &options, const std::string &auth_token) noexcept
  {
    if (auth_token.empty())
    {
      return;
    }

    options.headers.emplace_back("authorization", "Bearer " + auth_token);
  }

  static std::string join_vector_of_strings(const std::vector<std::string> &vec, const char joiner)
  {
    if (vec.empty())
      return "";

    return std::accumulate(
        std::next(vec.begin()), vec.end(), vec.front(),
        [joiner](const std::string &a, const std::string &b)
        {
          return a + joiner + b;
        });
  }

  void airport_add_flight_path_header(arrow::flight::FlightCallOptions &options,
                                      const arrow::flight::FlightDescriptor &descriptor)
  {
    if (descriptor.type == arrow::flight::FlightDescriptor::PATH)
    {
      auto path_parts = descriptor.path;
      std::string joined_path_parts = join_vector_of_strings(path_parts, '/');
      options.headers.emplace_back("airport-flight-path", joined_path_parts);
    }
  }

  void airport_add_trace_id_header(arrow::flight::FlightCallOptions &options,
                                   const string &trace_id)
  {
    options.headers.emplace_back("airport-trace-id", trace_id);
  }

  void airport_add_catalog_header(arrow::flight::FlightCallOptions &options,
                                  const std::string &catalog_name) noexcept
  {
    if (!catalog_name.empty())
    {
      options.headers.emplace_back("airport-catalog", catalog_name);
    }
  }

  void airport_add_normal_headers(arrow::flight::FlightCallOptions &options,
                                  const AirportTakeFlightParameters &params,
                                  const string &trace_id,
                                  const std::optional<arrow::flight::FlightDescriptor> &descriptor)
  {
    airport_add_standard_headers(options, params.server_location());
    airport_add_catalog_header(options, params.catalog_name());
    airport_add_authorization_header(options, params.auth_token());
    airport_add_trace_id_header(options, trace_id);

    for (const auto &header_pair : params.user_supplied_headers())
    {
      for (const auto &header_value : header_pair.second)
      {
        options.headers.emplace_back(header_pair.first, header_value);
      }
    }

    if (descriptor.has_value())
    {
      const auto &flight_descriptor = descriptor.value();
      airport_add_flight_path_header(options, flight_descriptor);
    }
  }
}