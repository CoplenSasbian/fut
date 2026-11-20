#pragma once

// fut/scannable.hpp -- the global scan contract.
//
// "The scanner sees me and calls my operation()."
//
// Any annotation with namespace-level work derives from this: the scanner
// walks a namespace, finds entities carrying scannable-derived annotations,
// and passes the entity's std::meta::info to the annotation's operation().
// What operation() does is entirely the annotation's business.
//
// Today fut::controller derives scannable (produces routes from a class).
// A future middleware, config injector, or anything with namespace-level
// behavior would also derive scannable -- without touching any scanner.
//
// This file has zero dependencies. Include it from anywhere.

#include <cstddef>
#include <string_view>

namespace fut {

struct scannable {};

inline constexpr std::size_t max_entity_annotations = 8;

} // namespace fut
