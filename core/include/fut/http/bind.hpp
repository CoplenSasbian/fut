#pragma once

// fut/http/bind.hpp -- parameter binding annotations.
//
// Read by the invoke machinery while building arguments; the scanner never
// sees them. NOT scannable-derived.

#include <cstddef>
#include <string_view>

#include <fut/http/route.hpp>

namespace fut {

inline constexpr std::size_t annot_key_max = 64;

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

// A form field: application/x-www-form-urlencoded, or the text parts of
// multipart/form-data. An empty key falls back to the parameter name.
struct from_form {
    detail::text_buf<annot_key_max> key;
    constexpr from_form() = default;
    constexpr from_form(std::string_view k) : key(k) {}
};

// An uploaded file from multipart/form-data. The parameter type must be
// fut::uploaded_file. An empty key falls back to the parameter name.
struct from_file {
    detail::text_buf<annot_key_max> key;
    constexpr from_file() = default;
    constexpr from_file(std::string_view k) : key(k) {}
};

} // namespace fut
