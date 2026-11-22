#pragma once

// fut/scannable.hpp -- the global scan contract.
//
// An annotation derives this when it has work to do at scan time. The work
// is an ordinary member function named `operation` on the annotation
// itself, in either shape:
//
//     struct my_annotation : fut::scannable {
//         int priority = 0;
//
//         template <std::meta::info Entity>              // entity-aware
//         void operation(fut::fut_ctx* ctx) const;
//     };
//
//     struct my_global_annotation : fut::scannable {
//         void operation(fut::fut_ctx* ctx) const;       // entity-agnostic
//     };
//
// The scanner finds an annotated entity, materialises the annotation value
// that was attached to it, reflects its members to find `operation`, and
// splices the call
//
//     annotation.template [:operation:]<Entity>(ctx)     // entity-aware
//     annotation.[:operation:](ctx)                      // entity-agnostic
//
// so the annotation receives exactly two things: the entity it is attached
// to (as a template argument, when it asks for one) and the context. The annotation's own members
// are simply `this` -- its configured path, key, priority, whatever it
// carries. Nothing else is threaded through: the app prefix, the app name
// and every server service come from the context, which keeps the scanner
// ignorant of what an annotation means.
//
// fut::controller (fut/http/annotate.hpp) is the built-in implementation:
// it reads the verb annotations off the class's member functions and
// registers routes with the router module.

#include <meta>

#include <fut/ctx.hpp>

namespace fut {

struct scannable {};

} // namespace fut
