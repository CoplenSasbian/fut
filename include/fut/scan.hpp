#pragma once

// fut/scan.hpp -- the generic namespace scanner.
//
//     fut::scan::run<^^admin>(ctx, "/api/admin");
//
// Recursion rule (exactly one level, namespaces only):
//
//   * the scanner looks at the DIRECT members of the namespace;
//   * a member that is a namespace is the ONLY thing it recurses into;
//   * any other member (class, function, variable, enum, ...) is never
//     descended into -- it is handed to the annotation found on it, once,
//     and that is the end of it.
//
// So a controller class nested inside another class is invisible to the
// scan, while a controller in a nested namespace is found. The annotation's
// operation<Entity>() decides what to do with the entity it was given.
//
// Nothing here knows about HTTP: a new scannable annotation needs no change
// to this file.

#include <array>
#include <cstddef>
#include <meta>
#include <string_view>
#include <utility>

#include <fut/scannable.hpp>

namespace fut::scan {

namespace meta = std::meta;

inline constexpr std::size_t max_ns_members = 32;

// Scannable-derived annotations on an entity, copied into a constexpr array
// (splices and template arguments need a named constant).
//
// Some namespace members cannot be queried (an unresolved overload set is
// one): annotations_of throws for those. They carry no annotation, so
// skipping them is correct -- the catch is the guard, not an error path.
template <std::size_t N, meta::info Entity>
consteval std::array<meta::info, N> entity_ops() {
    std::array<meta::info, N> out{};
    std::size_t i = 0;
    try {
        for (auto a : meta::annotations_of(Entity))
            if (i < N && meta::is_base_of_type(^^fut::scannable, meta::type_of(a)))
                out[i++] = a;
    } catch (...) {
    }
    return out;
}

// The direct members of a namespace, in declaration order.
template <std::size_t N, meta::info Ns>
consteval std::array<meta::info, N> ns_members() {
    std::array<meta::info, N> out{};
    std::size_t i = 0;
    for (auto m : meta::members_of(Ns, meta::access_context::unchecked()))
        if (i < N) out[i++] = m;
    return out;
}

// Hand one annotated entity to one annotation.
template <meta::info Ann, meta::info Entity>
void call_op(fut_ctx* ctx, std::string_view prefix) {
    using Op = [: meta::dealias(meta::type_of(Ann)) :];
    Op::template operation<Entity>(ctx, prefix);
}

template <meta::info Entity, std::size_t A>
void run_entity(fut_ctx* ctx, std::string_view prefix) {
    constexpr auto ops = entity_ops<max_entity_annotations, Entity>();
    if constexpr (A < max_entity_annotations && ops[A] != meta::info{})
        call_op<ops[A], Entity>(ctx, prefix);
}

// Every scannable annotation on one entity, never descending into it.
template <meta::info Entity>
void run_entity(fut_ctx* ctx, std::string_view prefix) {
    [&]<std::size_t... A>(std::index_sequence<A...>) {
        (run_entity<Entity, A>(ctx, prefix), ...);
    }(std::make_index_sequence<max_entity_annotations>());
}

template <meta::info Ns>
void run(fut_ctx* ctx, std::string_view prefix);

template <meta::info Ns, std::size_t M>
void run_member(fut_ctx* ctx, std::string_view prefix) {
    constexpr auto ms = ns_members<max_ns_members, Ns>();
    if constexpr (M < max_ns_members && ms[M] != meta::info{}) {
        constexpr meta::info m = ms[M];
        if constexpr (meta::is_namespace(m))
            run<m>(ctx, prefix);              // the only recursion
        else
            run_entity<m>(ctx, prefix);       // handed over, never descended
    }
}

template <meta::info Ns>
void run(fut_ctx* ctx, std::string_view prefix) {
    [&]<std::size_t... M>(std::index_sequence<M...>) {
        (run_member<Ns, M>(ctx, prefix), ...);
    }(std::make_index_sequence<max_ns_members>());
}

} // namespace fut::scan
