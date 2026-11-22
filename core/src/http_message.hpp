#pragma once

// HTTP/1.1 message types used by the server's connection loop.
//
// Parsing is done by llhttp (http_parser.hpp); serialization lives here
// because a response is only a status line, the headers and a body. No
// template metaprogramming, no heavy dependency: the message layer is
// plain data plus string building.

#include <cstddef>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace fut::exec {

struct http_request_message {
    std::string method{};
    std::string target{};     // request target as sent (path + query)
    std::string version{};    // "HTTP/1.1"
    std::vector<std::pair<std::string, std::string>> headers{};
    std::string body{};
    bool keep_alive{true};

    // Case-insensitive header lookup (HTTP header names are). Returns an
    // empty view when absent.
    [[nodiscard]] std::string_view header(std::string_view name) const;

    void clear();
};

// Status line reason, e.g. 200 -> "OK".
[[nodiscard]] std::string_view reason_phrase(unsigned status);

// Status line + headers + body, ready for the socket. The Connection
// header and Content-Length are always written by this function.
[[nodiscard]] std::string serialize_response(
    unsigned status, std::string_view content_type,
    const std::vector<std::pair<std::string, std::string>>& extra_headers,
    std::string_view body, bool keep_alive);

} // namespace fut::exec
