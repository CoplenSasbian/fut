#pragma once

// fut/scannable.hpp -- the global scan contract.
//
// An annotation derives this when it has work to do at scan time. The work
// is an ordinary member function on the annotation itself:
//
//     struct my_annotation : fut::scannable {
//         int priority = 0;
//
//         template <std::meta::info Entity>
//         void operation(fut::fut_ctx* ctx) const;
//     };
//
// The scanner finds an annotated entity, materialises the annotation value
// that was attached to it, and calls
//
//     annotation.template operation<Entity>(ctx)
//
// so the annotation receives exactly two things: the entity it is attached
// to (as a template argument) and the context. The annotation's own members
// are simply `this` -- its configured path, key, priority, whatever it
// carries. Nothing else is threaded through: the app prefix, the app name
// and every server service come from the context, which keeps the scanner
// ignorant of what an annotation means.
//
// fut::controller (fut/http/annotate.hpp) is the built-in implementation:
// it reads the verb annotations off the class's member functions and
// registers routes with the router module.

#include <cstddef>
#include <meta>

#include <fut/ctx.hpp>

namespace fut {

struct scannable {};

inline constexpr std::size_t max_entity_annotations = 8;

} // namespace fut
