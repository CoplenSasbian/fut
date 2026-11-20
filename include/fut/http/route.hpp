#pragma once

// fut/http/route.hpp -- the HTTP vocabulary: request/response, path matching,
// route records, app tables, the method enum, and the scannable contract.
//
// This file has no dependencies beyond the Asio awaitable (used in the
// route_record's invoke signature) and the standard library.

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <boost/asio/awaitable.hpp>

namespace fut {

inline constexpr std::uint32_t abi_version = 1;
inline constexpr const char* library_version = "0.2.0";

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
// scannable -- "the namespace scanner sees me and calls my operation()"
//
// ONLY annotations with namespace-level work derive from this. Today that
// is fut::controller: the scanner finds controller-annotated CLASS TYPES,
// passes the class's std::meta::info to controller::operation(), which
// walks the class's member functions for http_action annotations.
// ---------------------------------------------------------------------------

struct scannable {};

inline constexpr std::size_t max_entity_annotations = 8;

// ---------------------------------------------------------------------------
// request / response
// ---------------------------------------------------------------------------

struct request {
    std::string_view method{};   // "GET" / "POST" / ...
    std::string_view target{};   // raw request target (with query)
    std::string_view path{};     // sub-path relative to the app prefix
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

struct bind_error : std::runtime_error {
    explicit bind_error(std::string_view what) : std::runtime_error(std::string(what)) {}
};

// ---------------------------------------------------------------------------
// route records / app table -- the DLL export surface
// ---------------------------------------------------------------------------

struct route_record {
    std::string_view method{};    // "GET" / "POST" / ...
    std::string_view pattern{};   // e.g. "/sum/{a}/{b}"
    boost::asio::awaitable<response> (*invoke)(const request&, const path_match&) = nullptr;
};

struct app_table {
    std::string_view name{};     // "admin"
    std::string_view prefix{};   // "/api/admin"
    const route_record* routes = nullptr;
    std::size_t route_count = 0;

    [[nodiscard]] std::span<const route_record> entries() const {
        return {routes, route_count};
    }
};

} // namespace fut
