#pragma once
// fut/route.hpp -- public ABI shared by the server executable and every app DLL.
//
// An "app" is a Django-style concept: a name, a URL prefix, and a set of
// annotated free functions ("actions"). Each app DLL exports one fut::app_table
// (see FUT_EXPORT_APP in fut/refl_router.hpp); the server dispatches
// (method, path) against the records of the loaded tables.
//
// All modules must be built with the same compiler and runtime (the CMake
// presets in this repository guarantee that). Bump fut::abi_version whenever
// this header changes in an incompatible way -- the server refuses to load
// DLLs whose ABI version does not match.

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
// request / response
// ---------------------------------------------------------------------------

// A request as handed to actions. All string_views point into buffers owned
// by the server and stay valid for the duration of the handler call.
struct request {
    std::string_view method{};   // "GET" / "POST" / ...
    std::string_view target{};   // raw request target (with query)
    std::string_view path{};     // sub-path relative to the app prefix; "/" when exactly on it
    std::string_view query{};    // everything after '?'
    std::string_view body{};     // request body
    std::span<const std::pair<std::string_view, std::string_view>> headers{};
};

// Built by an action (or by the framework); ownership moves to the server.
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
// path matching: literal segments must match, "{name}" segments are captured
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

// Thrown by the framework when parameter binding fails (invalid JSON body,
// missing query/path/header value); the server turns it into a 400 response.
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
