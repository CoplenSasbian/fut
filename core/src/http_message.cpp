#include "http_message.hpp"

#include <algorithm>
#include <cctype>
#include <string>

namespace fut::exec {
namespace {

bool iequals(std::string_view a, std::string_view b) {
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i)
        if (std::tolower(static_cast<unsigned char>(a[i])) !=
            std::tolower(static_cast<unsigned char>(b[i])))
            return false;
    return true;
}

} // namespace

std::string_view http_request_message::header(std::string_view name) const {
    for (const auto& [key, value] : headers)
        if (iequals(key, name)) return value;
    return {};
}

void http_request_message::clear() {
    method.clear();
    target.clear();
    version.clear();
    header_bytes.clear();   // capacity kept: the next request reuses it
    headers.clear();
    body.clear();
    keep_alive = true;
}

std::string_view reason_phrase(unsigned status) {
    switch (status) {
    case 100: return "Continue";
    case 200: return "OK";
    case 201: return "Created";
    case 202: return "Accepted";
    case 204: return "No Content";
    case 301: return "Moved Permanently";
    case 302: return "Found";
    case 304: return "Not Modified";
    case 400: return "Bad Request";
    case 401: return "Unauthorized";
    case 403: return "Forbidden";
    case 404: return "Not Found";
    case 405: return "Method Not Allowed";
    case 408: return "Request Timeout";
    case 409: return "Conflict";
    case 413: return "Content Too Large";
    case 415: return "Unsupported Media Type";
    case 429: return "Too Many Requests";
    case 500: return "Internal Server Error";
    case 501: return "Not Implemented";
    case 503: return "Service Unavailable";
    }
    return "Status";
}

std::string serialize_response(
    unsigned status, std::string_view content_type,
    const std::vector<std::pair<std::string, std::string>>& extra_headers,
    std::string_view body, bool keep_alive) {
    std::string out;
    serialize_response(out, status, content_type, extra_headers, body, keep_alive);
    return out;
}

void serialize_response(std::string& out, unsigned status, std::string_view content_type,
                        const std::vector<std::pair<std::string, std::string>>& extra_headers,
                        std::string_view body, bool keep_alive) {
    out.clear();   // capacity is kept on purpose: one buffer per connection

    out += "HTTP/1.1 ";
    out += std::to_string(status);
    out += ' ';
    out += reason_phrase(status);
    out += "\r\n";

    out += "Content-Type: ";
    out += content_type;
    out += "\r\n";
    out += "Content-Length: ";
    out += std::to_string(body.size());
    out += "\r\n";
    out += keep_alive ? "Connection: keep-alive\r\n" : "Connection: close\r\n";

    for (const auto& [name, value] : extra_headers) {
        out += name;
        out += ": ";
        out += value;
        out += "\r\n";
    }
    out += "\r\n";
    out.append(body);
}

} // namespace fut::exec
