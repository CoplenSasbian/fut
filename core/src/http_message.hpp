#pragma once

// HTTP/1.1 message types used by the server's connection loop.
//
// Parsing is done by llhttp (http_parser.hpp); serialization lives here
// because a response is only a status line, the headers and a body. No
// template metaprogramming, no heavy dependency: the message layer is
// plain data plus string building.

#include <cstddef>
#include <string>
#include <span>
#include <string_view>
#include <utility>
#include <vector>

namespace fut::exec {

struct http_request_message {
    std::string method{};
    std::string target{};     // request target as sent (path + query)
    std::string version{};    // "HTTP/1.1"

    // Header names and values live here, back to back; `headers` only records
    // where each one is. One string instead of 2N small strings means the
    // storage is reused across requests on a connection (clear() keeps the
    // capacity), so parsing headers stops allocating after the first request.
    std::string header_bytes{};
    std::vector<std::pair<std::string_view, std::string_view>> headers{};

    std::string body{};
    bool keep_alive{true};

    // Case-insensitive header lookup (HTTP header names are). Returns an
    // empty view when absent.
    [[nodiscard]] std::string_view header(std::string_view name) const;

    // Recycles the message for the next request on the same connection: the
    // vectors and the body keep their capacity, so a keep-alive connection
    // stops allocating after the first request.
    void clear();

    [[nodiscard]] std::span<const std::pair<std::string_view, std::string_view>> header_span()
        const {
        return headers;
    }

};

// Status line reason, e.g. 200 -> "OK".
[[nodiscard]] std::string_view reason_phrase(unsigned status);

// Status line + headers + body, ready for the socket. The Connection
// header and Content-Length are always written by this function.
//
// The overload that fills `out` keeps its capacity between calls, which is how
// the connection loop serializes every response into one reused buffer: no
// allocation per request once it has grown to the largest response.
[[nodiscard]] std::string serialize_response(
    unsigned status, std::string_view content_type,
    const std::vector<std::pair<std::string, std::string>>& extra_headers,
    std::string_view body, bool keep_alive);

void serialize_response(
    std::string& out, unsigned status, std::string_view content_type,
    const std::vector<std::pair<std::string, std::string>>& extra_headers,
    std::string_view body, bool keep_alive);

} // namespace fut::exec
