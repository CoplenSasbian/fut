#pragma once

// fut/http/route.hpp -- the HTTP vocabulary: request/response, the method
// enum, path matching, the bind error, and path composition.
//
// No DLL export surface lives here any more: apps register themselves
// through fut::http::router_module, obtained from the context
// (see fut/ctx.hpp and fut/http/router.hpp).

#include <array>
#include <fut/detail/text.hpp>

#include <cstddef>
#include <cctype>
#include <cstdint>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <fut/export.hpp>
#include <fut/http/response.hpp>
#include <fut/url.hpp>

namespace fut {

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
// request / response
// ---------------------------------------------------------------------------

namespace detail {

inline bool iequals(std::string_view a, std::string_view b) {
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i) {
        const auto ca = static_cast<unsigned char>(a[i]);
        const auto cb = static_cast<unsigned char>(b[i]);
        if (std::tolower(ca) != std::tolower(cb)) return false;
    }
    return true;
}

} // namespace fut::detail

// The request, as an action sees it. Reading anything here needs no binding
// annotation: the whole request is handed to the handler.
struct request {
    std::string_view method{};   // "GET" / "POST" / ...
    std::string_view target{};   // raw request target (with query)
    std::string_view path{};     // path part of the target
    std::string_view query{};    // everything after '?'
    std::string_view body{};     // request body
    std::span<const std::pair<std::string_view, std::string_view>> headers{};
    // Peer, "ip:port" (set by the server). Last so the other fields keep their
    // aggregate order.
    std::string_view remote{};

    // ---- headers (case-insensitive, as HTTP is) ----
    [[nodiscard]] std::string_view header(std::string_view name) const {
        for (const auto& [key, value] : headers)
            if (detail::iequals(key, name)) return value;
        return {};
    }
    [[nodiscard]] bool has_header(std::string_view name) const {
        for (const auto& [key, value] : headers)
            if (detail::iequals(key, name)) return true;
        return false;
    }
    [[nodiscard]] std::string_view header_or(std::string_view name,
                                             std::string_view fallback) const {
        const std::string_view v = header(name);
        return v.empty() ? fallback : v;
    }
    [[nodiscard]] std::string_view content_type() const { return header("Content-Type"); }

    // ---- method ----
    // NB: fully qualified -- the 'method' data member above would hide the enum.
    [[nodiscard]] std::optional<fut::method> verb() const { return method_from_string(method); }
    [[nodiscard]] bool is(fut::method m) const { return verb() == m; }

    // ---- query string ----
    // param() decodes, so "name=John+Doe" reads back as "John Doe".
    [[nodiscard]] std::optional<std::string> param(std::string_view name) const {
        for (const auto& [key, value] : parse_query(query))
            if (key == name) return value;
        return std::nullopt;
    }
    [[nodiscard]] std::string param_or(std::string_view name, std::string_view fallback) const {
        if (auto v = param(name)) return *v;
        return std::string(fallback);
    }
    [[nodiscard]] bool has_param(std::string_view name) const { return param(name).has_value(); }
    [[nodiscard]] std::vector<std::pair<std::string, std::string>> params() const {
        return parse_query(query);
    }

    // ---- body ----
    // Parsed from the JSON body; defined in <fut/json.hpp> so this header stays
    // glaze-free.
    template <class T>
    [[nodiscard]] std::optional<T> json() const;

    [[nodiscard]] std::string_view text() const { return body; }
    [[nodiscard]] bool empty_body() const { return body.empty(); }
};

// A request that owns its bytes.
//
// fut::request borrows: the connection keeps the read buffer, the parser keeps
// the message, and the views are valid until the action returns. That is what
// makes the hot path allocation-free. When a request must outlive the call --
// queued for later, handed to another thread, stored for an audit log -- take
// an owned snapshot deliberately:
//
//     fut::owned_request kept{req};          // one copy, at this point only
//     ...later...  kept.view().json<order>()
//
// view() rebuilds the header views on every call, so do not move an
// owned_request while one of its views is still in use.
struct owned_request {
    std::string method{};
    std::string target{};
    std::string path{};
    std::string query{};
    std::string body{};
    std::string remote{};
    std::vector<std::pair<std::string, std::string>> headers{};

    owned_request() = default;
    explicit owned_request(const request& r)
        : method(r.method),
          target(r.target),
          path(r.path),
          query(r.query),
          body(r.body),
          remote(r.remote) {
        headers.reserve(r.headers.size());
        for (const auto& [name, value] : r.headers)
            headers.emplace_back(name, value);
    }

    [[nodiscard]] request view() const {
        std::vector<std::pair<std::string_view, std::string_view>> views;
        views.reserve(headers.size());
        for (const auto& [name, value] : headers) views.emplace_back(name, value);
        // The vector is rebuilt below into a member so the span stays valid.
        const_cast<owned_request*>(this)->header_views = std::move(views);
        return request{method, target, path, query, body, header_views, remote};
    }

private:
    std::vector<std::pair<std::string_view, std::string_view>> header_views{};
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
    [[nodiscard]] bool has(std::string_view key) const { return !segment(key).empty(); }
    [[nodiscard]] std::size_t count() const noexcept { return named_count; }
    [[nodiscard]] std::span<const std::pair<std::string_view, std::string_view>> params() const {
        return {named.data(), named_count};
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
