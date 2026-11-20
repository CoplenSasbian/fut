#pragma once
// fut/annotate.hpp -- C++26 annotation types (P3394).
//
// Verbs (on free-function actions):
//   [[=fut::http_get ("/ping")]]        GET route
//   [[=fut::http_post("/echo")]]        POST route
//   [[=fut::http_put ("/x")]]           PUT route
//   [[=fut::http_delete("/x")]]         DELETE route
// Path patterns may capture segments: "/sum/{a}/{b}".
//
// Parameter binding annotations (on action parameters):
//   [[=fut::from_body{}]]         <- request body JSON, parsed into the parameter type
//   [[=fut::from_query("name")]]  <- query parameter (empty key -> parameter name)
//   [[=fut::from_path("a")]]      <- path "{placeholder}" segment (empty key -> parameter name)
//   [[=fut::from_header("X-K")]]  <- request header
//   no annotation + const fut::request& -> raw request (escape hatch)
//
// Annotation types must be structural (fixed char buffers, nothing on the heap).

#include <cstddef>
#include <string_view>

namespace fut {

inline constexpr std::size_t annot_path_max = 128;  // capacity for route patterns / keys
inline constexpr std::size_t annot_key_max = 64;

namespace detail {

// Fixed-capacity string buffer (structural: char array + constexpr constructors).
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

template <std::size_t Max>
struct verb_base {
    text_buf<Max> path;

    constexpr verb_base() = default;
    constexpr verb_base(std::string_view s) : path(s) {}
};

} // namespace detail

// ---- HTTP verb annotations ----
struct http_get    : detail::verb_base<annot_path_max> { using verb_base::verb_base; };
struct http_post   : detail::verb_base<annot_path_max> { using verb_base::verb_base; };
struct http_put    : detail::verb_base<annot_path_max> { using verb_base::verb_base; };
struct http_delete : detail::verb_base<annot_path_max> { using verb_base::verb_base; };

// ---- controller annotation (class-head position) ----
// Placed AFTER the struct keyword; its member functions' route patterns are
// joined onto this path. One instance per controller type is created lazily
// per DLL, so members may carry state.
//     struct [[=fut::controller("/users")]] users_controller { ... };
struct controller {
    detail::text_buf<annot_path_max> path;
    constexpr controller() = default;
    constexpr controller(std::string_view s) : path(s) {}
};

// ---- parameter binding annotations ----
struct from_body {};

struct from_query {
    detail::text_buf<annot_key_max> key;
    constexpr from_query() = default;
    constexpr from_query(std::string_view k) : key(k) {}
};

struct from_path {
    detail::text_buf<annot_key_max> key;
    constexpr from_path() = default;
    constexpr from_path(std::string_view k) : key(k) {}
};

struct from_header {
    detail::text_buf<annot_key_max> key;
    constexpr from_header() = default;
    constexpr from_header(std::string_view k) : key(k) {}
};

} // namespace fut
