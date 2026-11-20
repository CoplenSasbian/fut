#pragma once

// fut/scannable.hpp -- the global scan contract.
//
// An annotation derives this when it has work to do at scan time:
//
//     struct my_annotation : fut::scannable {
//         template <std::meta::info Entity>
//         static void operation(fut::fut_ctx* ctx, std::string_view prefix);
//     };
//
// A scanner walks a namespace and, for every CLASS TYPE carrying a
// scannable-derived annotation, calls that annotation's operation<Entity>(),
// passing the entity it was found on, the context, and the app prefix (the
// add_app() prefix, injected as the FUT_APP_PREFIX compile definition).
// The scanner knows nothing about what operation() does -- routes, config,
// code generation, anything.
//
// fut::controller (fut/http/annotate.hpp) is the built-in implementation:
// it reads the verb annotations off the class's member functions and
// registers routes with the router module.

#include <cstddef>
#include <meta>
#include <string_view>

#include <fut/ctx.hpp>

namespace fut {

struct scannable {};

inline constexpr std::size_t max_entity_annotations = 8;

} // namespace fut
