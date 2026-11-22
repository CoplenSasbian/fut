#include "http_message.hpp"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <ctime>

#include <fut/platform.hpp>
#include <mutex>
#include <string>

namespace fut::exec {

namespace {

// A header that still contains CR/LF (set through a path that bypassed the
// response setters) must not reach the wire.
bool has_ctl(std::string_view s) {
    return s.find_first_of("\r\n") != std::string_view::npos;
}

} // namespace
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

// RFC 7231 IMF-fixdate, cached per second: a response without Date is legal
// but every proxy and cache wants one, and the format is O(1) to reuse.
namespace {

std::string_view http_date() {
    static std::mutex m;
    static std::string cached;
    static std::time_t cached_at = 0;

    const std::time_t now = std::time(nullptr);
    std::lock_guard<std::mutex> lock(m);
    if (now != cached_at) {
        const std::tm tm = fut::platform::utc_time(now);
        char buf[32];
        std::strftime(buf, sizeof(buf), "%a, %d %b %Y %H:%M:%S GMT", &tm);
        cached = buf;
        cached_at = now;
    }
    return cached;
}

} // namespace

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
    case 411: return "Length Required";
    case 414: return "URI Too Long";
    case 409: return "Conflict";
    case 413: return "Content Too Large";
    case 415: return "Unsupported Media Type";
    case 429: return "Too Many Requests";
    case 431: return "Request Header Fields Too Large";
    case 500: return "Internal Server Error";
    case 501: return "Not Implemented";
    case 503: return "Service Unavailable";
    }
    return "Status";
}

std::string serialize_response(
    unsigned status, std::string_view content_type,
    const std::vector<std::pair<std::string, std::string>>& extra_headers,
    std::string_view body, bool keep_alive, bool head_only) {
    std::string out;
    serialize_response(out, status, content_type, extra_headers, body, keep_alive, head_only);
    return out;
}

void serialize_stream_head(std::string& out, unsigned status, std::string_view content_type,
                           const std::vector<std::pair<std::string, std::string>>& extra_headers) {
    out.clear();
    out += "HTTP/1.1 ";
    out += std::to_string(status);
    out += ' ';
    out += reason_phrase(status);
    out += "\r\n";
    out += "Content-Type: ";
    out += content_type;
    out += "\r\n";
    out += "Date: ";
    out += http_date();
    out += "\r\n";
    out += "Transfer-Encoding: chunked\r\n";
    for (const auto& [name, value] : extra_headers) {
        if (has_ctl(name) || has_ctl(value)) continue;
        out += name;
        out += ": ";
        out += value;
        out += "\r\n";
    }
    out += "\r\n";
}

void append_chunk(std::string& out, std::string_view bytes) {
    if (bytes.empty()) return;
    char size[24];
    std::snprintf(size, sizeof(size), "%zx", bytes.size());
    out += size;
    out += "\r\n";
    out.append(bytes);
    out += "\r\n";
}

void append_last_chunk(std::string& out) { out += "0\r\n\r\n"; }

void serialize_response(std::string& out, unsigned status, std::string_view content_type,
                        const std::vector<std::pair<std::string, std::string>>& extra_headers,
                        std::string_view body, bool keep_alive, bool head_only) {
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
    out += "Date: ";
    out += http_date();
    out += "\r\n";

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
