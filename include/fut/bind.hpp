#pragma once

// fut/bind.hpp -- parameter binding annotations (invoke-machinery vocabulary).
//
// These are NOT annotation_base-derived: they are read while the invoke
// machinery builds arguments, not scanned as route producers.

#include <cstddef>

#include <fut/route.hpp>
#include <string_view>

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

} // namespace fut
