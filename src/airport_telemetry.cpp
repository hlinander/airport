// Blocking Flight call intended for the host's telemetry worker. It has no
// DuckDB connection dependency, so a Python CTAS can execute concurrently.
#include "storage/airport_catalog_api.hpp"
#include "airport_request_headers.hpp"
#include "yyjson.hpp"
#include <arrow/flight/client.h>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <memory>

using namespace duckdb_yyjson;

namespace {
char *Copy(const std::string &text) {
    auto *out = static_cast<char *>(std::malloc(text.size() + 1));
    if (out) std::memcpy(out, text.c_str(), text.size() + 1);
    return out;
}
char *Failure() {
    // Do not include transport diagnostics which may echo request metadata.
    return Copy("{\"version\":1,\"error\":\"Remote execution telemetry unavailable\"}");
}
char *Failure(const arrow::Status &status) {
    // Older Swanlake releases reject unknown action names with InvalidArgument.
    const auto message = status.ToString();
    if (status.IsNotImplemented() ||
        message.find("do_action: The defined request is invalid: \"execution_updates\"") != std::string::npos) {
        return Copy("{\"version\":1,\"unsupported\":true}");
    }
    return Failure();
}
}

#ifdef _MSC_VER
#define AT_EXPORT extern "C" __declspec(dllexport)
#else
#define AT_EXPORT extern "C" __attribute__((visibility("default")))
#endif

AT_EXPORT char *airport_execution_updates(const char *endpoint, const char *headers, const char *request) {
    try {
        if (!endpoint || !headers || !request || std::strlen(headers) > 65536 || std::strlen(request) > 16384) return Failure();
        std::unique_ptr<yyjson_doc, decltype(&yyjson_doc_free)> doc(
            yyjson_read(headers, std::strlen(headers), 0), yyjson_doc_free);
        if (!doc || !yyjson_is_arr(yyjson_doc_get_root(doc.get()))) return Failure();
        arrow::flight::FlightCallOptions options;
        options.timeout = std::chrono::seconds(2);
        bool pinned_session = false;
        size_t index, count;
        yyjson_val *pair;
        yyjson_arr_foreach(yyjson_doc_get_root(doc.get()), index, count, pair) {
            const char *key = yyjson_get_str(yyjson_arr_get(pair, 0));
            const char *value = yyjson_get_str(yyjson_arr_get(pair, 1));
            if (!key || !value) return Failure();
            options.headers.emplace_back(key, value);
            if (std::strcmp(key, "airport-client-session-id") == 0) pinned_session = true;
        }
        // The host pins this ID when a run starts; session rotation must not
        // silently retarget an outstanding poll.
        if (!pinned_session) return Failure();
        auto client = duckdb::AirportAPI::FlightClientForLocation(endpoint);
        arrow::flight::Action action {"execution_updates", arrow::Buffer::FromString(request)};
        auto stream_result = client->DoAction(options, action);
        if (!stream_result.ok()) return Failure(stream_result.status());
        auto stream = std::move(stream_result).ValueOrDie();
        auto next = stream->Next();
        if (!next.ok()) return Failure(next.status());
        auto result = std::move(next).ValueOrDie();
        if (!result || !result->body || result->body->size() > 4 * 1024 * 1024) return Failure();
        return Copy(result->body->ToString());
    } catch (...) { return Failure(); }
}

AT_EXPORT char *airport_execution_session_id() {
    try { return Copy(duckdb::airport_last_client_session_id()); } catch (...) { return nullptr; }
}
AT_EXPORT void airport_execution_free_string(char *text) { std::free(text); }
