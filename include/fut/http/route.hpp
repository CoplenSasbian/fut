#pragma once

// fut/http/route.hpp -- the HTTP vocabulary: request/response, the method
// enum, path matching, the bind error, and path composition.
//
// No DLL export surface lives here any more: apps register themselves
// through fut::http::router_module, obtained from the context
// (see fut/ctx.hpp and fut/http/router.hpp).

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace fut {

inline constexpr std::uint32_t abi_version = 1;
inline constexpr const char* library_version = "0.3.0";

#if defined(_WIN32)
#  define FUT_EXPORT __declspec(dllexport)
#else
#  define FUT_EXPORT __attribute__((visibility("default")))
#endif

// ---------------------------------------------------------------------------
// method -- the HTTP verb
// ---------------------------------------------------------------------------

enum class method : std::uint8_t {
    GET,
    POST,
    PUT,
    DEL,   // "DELETE" is a Windows macro
};

constexpr std::string_view to_string(method m) {
    switch (m) {
    case method::GET:  return "GET";
    case method::POST: return "POST";
    case method::PUT:  return "PUT";
    case method::DEL:  return "DELETE";
    }
    return "UNKNOWN";
}

inline std::optional<method> method_from_string(std::string_view s) {
    if (s == "GET") return method::GET;
    if (s == "POST") return method::POST;
    if (s == "PUT") return method::PUT;
    if (s == "DELETE") return method::DEL;
    return std::nullopt;
}

// ---------------------------------------------------------------------------
// detail::text_buf -- fixed-capacity string buffer (structural)
// ---------------------------------------------------------------------------

namespace detail {

template <std::size_t Max>
struct text_buf {
    char chars[Max]{};

    constexpr text_buf() = default;
    constexpr text_buf(std::string_view s) {
        for (std::size_t i = 0; i < s.size() && i + 1 < Max; ++i) chars[i] = s[i];
    }
    constexpr std::size_t size() const {
        std::size_t n = 0;
        while (n < Max && chars[n]) ++n;
        return n;
    }
    constexpr std::string_view view() const { return {chars, size()}; }
};

} // namespace fut::detail

// ---------------------------------------------------------------------------
// request / response
// ---------------------------------------------------------------------------

struct request {
    std::string_view method{};   // "GET" / "POST" / ...
    std::string_view target{};   // raw request target (with query)
    std::string_view path{};     // path part of the target
    std::string_view query{};    // everything after '?'
    std::string_view body{};     // request body
    std::span<const std::pair<std::string_view, std::string_view>> headers{};
};

struct response {
    unsigned status = 200;
    std::string content_type = "application/json";
    std::string body{};
    std::vector<std::pair<std::string, std::string>> headers{};

    response() = default;
    response(unsigned s, std::string ct, std::string b)
        : status(s), content_type(std::move(ct)), body(std::move(b)) {}
};

// ---------------------------------------------------------------------------
// path matching: literal segments match, "{name}" segments capture
// ---------------------------------------------------------------------------

struct path_match {
    static constexpr std::size_t max_named = 8;
    std::array<std::pair<std::string_view, std::string_view>, max_named> named{};
    std::size_t named_count = 0;

    [[nodiscard]] std::string_view segment(std::string_view key) const {
        for (std::size_t i = 0; i < named_count; ++i)
            if (named[i].first == key) return named[i].second;
        return {};
    }
};

inline bool match_pattern(std::string_view pattern, std::string_view path, path_match& out) {
    auto next_seg = [](std::string_view s, std::size_t& i) {
        while (i < s.size() && s[i] == '/') ++i;
        const std::size_t b = i;
        while (i < s.size() && s[i] != '/') ++i;
        return s.substr(b, i - b);
    };
    std::string_view pat[16];
    std::string_view seg[16];
    std::size_t np = 0, ns = 0, i = 0;
    for (std::string_view s = next_seg(pattern, i); !s.empty() && np < 16;
         s = next_seg(pattern, i))
        pat[np++] = s;
    i = 0;
    for (std::string_view s = next_seg(path, i); !s.empty() && ns < 16;
         s = next_seg(path, i))
        seg[ns++] = s;
    if (np != ns) return false;

    for (std::size_t k = 0; k < np; ++k) {
        const std::string_view p = pat[k];
        if (p.size() >= 2 && p.front() == '{' && p.back() == '}') {
            if (out.named_count >= path_match::max_named) return false;
            out.named[out.named_count++] = {p.substr(1, p.size() - 2), seg[k]};
        } else if (p != seg[k]) {
            return false;
        }
    }
    return true;
}

// Number of literal (non-placeholder) segments: the specificity of a pattern.
inline std::size_t literal_segments(std::string_view pattern) {
    std::size_t n = 0, i = 0;
    while (i < pattern.size()) {
        while (i < pattern.size() && pattern[i] == '/') ++i;
        const std::size_t b = i;
        while (i < pattern.size() && pattern[i] != '/') ++i;
        if (i == b) break;
        const std::string_view seg = pattern.substr(b, i - b);
        if (!(seg.size() >= 2 && seg.front() == '{' && seg.back() == '}')) ++n;
    }
    return n;
}

// ---------------------------------------------------------------------------
// join_path -- compose a route path from up to four parts, single-slashed
// ---------------------------------------------------------------------------

namespace detail {

inline void append_segment(std::string& out, std::string_view seg) {
    std::size_t begin = 0;
    std::size_t end = seg.size();
    while (begin < end && seg[begin] == '/') ++begin;
    while (end > begin && seg[end - 1] == '/') --end;
    if (begin == end) return;
    if (out.empty() || out.back() != '/') out += '/';
    out.append(seg.substr(begin, end - begin));
}

} // namespace fut::detail

inline std::string join_path(std::string_view a, std::string_view b,
                             std::string_view c = {}, std::string_view d = {}) {
    std::string out;
    detail::append_segment(out, a);
    detail::append_segment(out, b);
    detail::append_segment(out, c);
    detail::append_segment(out, d);
    if (out.empty()) out = "/";
    return out;
}

struct bind_error : std::runtime_error {
    explicit bind_error(std::string_view what) : std::runtime_error(std::string(what)) {}
};

} // namespace fut
