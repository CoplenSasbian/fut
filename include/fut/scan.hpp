#pragma once

// fut/scan.hpp -- the generic namespace scanner.
//
//     fut::scan::run<^^admin>(ctx, "/api/admin");
//
// walks namespace admin (recursively), finds every class type carrying a
// scannable-derived annotation, and calls
//
//     Annotation::operation<TheClass>(ctx, prefix)
//
// once per annotation. An app DLL's exported fut_app_register() is exactly
// this call -- see FUT_EXPORT_APP_NS in fut/http/http.hpp.
//
// Nothing here knows about HTTP or routes: a new scannable annotation needs
// no change to this file.

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
template <std::size_t N, meta::info Entity>
consteval std::array<meta::info, N> entity_ops() {
    std::array<meta::info, N> out{};
    std::size_t i = 0;
    for (auto a : meta::annotations_of(Entity))
        if (i < N && meta::is_base_of_type(^^fut::scannable, meta::type_of(a)))
            out[i++] = a;
    return out;
}

// Scannable-annotated class types plus nested namespaces of a namespace.
// Functions and everything else are invisible to the scan by construction.
template <std::size_t N, meta::info Ns>
consteval std::array<meta::info, N> ns_targets() {
    std::array<meta::info, N> out{};
    std::size_t i = 0;
    for (auto m : meta::members_of(Ns, meta::access_context::unchecked())) {
        if (i >= N) break;
        if (meta::is_namespace(m)) {
            out[i++] = m;
        } else if (meta::is_type(m) && meta::is_class_type(m)) {
            for (auto a : meta::annotations_of(m))
                if (meta::is_base_of_type(^^fut::scannable, meta::type_of(a))) {
                    out[i++] = m;
                    break;
                }
        }
    }
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

template <meta::info Entity>
void run_class(fut_ctx* ctx, std::string_view prefix) {
    [&]<std::size_t... A>(std::index_sequence<A...>) {
        (run_entity<Entity, A>(ctx, prefix), ...);
    }(std::make_index_sequence<max_entity_annotations>());
}

template <meta::info Ns>
void run(fut_ctx* ctx, std::string_view prefix);

template <meta::info Ns, std::size_t M>
void run_member(fut_ctx* ctx, std::string_view prefix) {
    constexpr auto ms = ns_targets<max_ns_members, Ns>();
    if constexpr (M < max_ns_members && ms[M] != meta::info{}) {
        constexpr meta::info m = ms[M];
        if constexpr (meta::is_namespace(m)) run<m>(ctx, prefix);
        else run_class<m>(ctx, prefix);
    }
}

template <meta::info Ns>
void run(fut_ctx* ctx, std::string_view prefix) {
    [&]<std::size_t... M>(std::index_sequence<M...>) {
        (run_member<Ns, M>(ctx, prefix), ...);
    }(std::make_index_sequence<max_ns_members>());
}

} // namespace fut::scan
