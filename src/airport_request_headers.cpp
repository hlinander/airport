#include "airport_request_headers.hpp"
#include <string.h>
#include "duckdb/common/types/uuid.hpp"
#include "duckdb/main/client_context.hpp"
#include "duckdb/main/client_context_state.hpp"
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

  // The client session id sent as `airport-client-session-id`. Servers that
  // bind authorization state to the session id (e.g. a workspace-scoped
  // session bound to (subject, project)) need each concurrent client
  // connection to carry a distinct, stable id: a process hosting several
  // subjects at once (the duckvis chat-runner runs many bots, each its own
  // subject, in one process) must not let one connection's id bind a session
  // the next connection then presents under a different subject. The id is
  // therefore stored per ClientContext, not process-global; the embedding
  // application can start a fresh session for a connection whose auth context
  // changes via airport_regenerate_client_session_id().
  struct AirportClientSessionState : public ClientContextState
  {
    std::mutex mutex;
    std::string session_id;
    AirportClientSessionState() : session_id(UUID::ToString(UUID::GenerateRandomUUID())) {}
  };

  static const char *AIRPORT_CLIENT_SESSION_STATE_KEY = "airport_client_session";

  static shared_ptr<AirportClientSessionState> airport_client_session_state(ClientContext &context)
  {
    return context.registered_state->GetOrCreate<AirportClientSessionState>(AIRPORT_CLIENT_SESSION_STATE_KEY);
  }

  // The id most recently emitted on a request header. Serves the exported,
  // context-free airport_execution_session_id(): the embedder pins it so a
  // kernel-telemetry execution-updates poll reaches the same swanlake session
  // its query connection established. With one active connection it tracks that
  // connection; it never feeds the per-connection header binding above.
  static std::mutex airport_last_session_id_mutex;
  static std::string airport_last_session_id = UUID::ToString(UUID::GenerateRandomUUID());

  std::string airport_last_client_session_id()
  {
    std::lock_guard<std::mutex> guard(airport_last_session_id_mutex);
    return airport_last_session_id;
  }

  std::string airport_client_session_id(ClientContext &context)
  {
    std::string id;
    {
      auto state = airport_client_session_state(context);
      std::lock_guard<std::mutex> guard(state->mutex);
      id = state->session_id;
    }
    {
      std::lock_guard<std::mutex> guard(airport_last_session_id_mutex);
      airport_last_session_id = id;
    }
    return id;
  }

  std::string airport_regenerate_client_session_id(ClientContext &context)
  {
    auto state = airport_client_session_state(context);
    std::lock_guard<std::mutex> guard(state->mutex);
    state->session_id = UUID::ToString(UUID::GenerateRandomUUID());
    return state->session_id;
  }

  static void
  airport_add_headers(std::vector<std::pair<std::string, std::string>> &headers, const std::string &server_location, ClientContext &context) noexcept
  {
    headers.emplace_back("airport-user-agent", AIRPORT_USER_AGENT);
    headers.emplace_back("authority", server_location);
    headers.emplace_back("airport-client-session-id", airport_client_session_id(context));
  }

  void airport_add_standard_headers(arrow::flight::FlightCallOptions &options, const std::string &server_location, ClientContext &context) noexcept
  {
    airport_add_headers(options.headers, server_location, context);
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
                                  ClientContext &context,
                                  const std::optional<arrow::flight::FlightDescriptor> &descriptor)
  {
    airport_add_standard_headers(options, params.server_location(), context);
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