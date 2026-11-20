#pragma once

// fut/http/refl_router.hpp -- the GENERIC namespace scanner.
//
// Walks the app namespace, finds CLASS TYPES carrying scannable-derived
// annotations (std::meta::is_base_of_type), and delegates to each
// annotation's static operation(). The scanner NEVER touches functions --
// verbs (http_get etc.) are http_action values read by the controller's
// operation(), not by the namespace scan.
//
// Adding a new scannable annotation: derive fut::scannable, implement
// operation(), place the annotated class in the app namespace. No scanner
// changes, no CMake changes.

#include <array>
#include <cstddef>
#include <string_view>
#include <type_traits>
#include <utility>

#include <meta>

#if !defined(__GNUC__) || defined(__clang__) || __GNUC__ < 15
#  error "fut requires C++26 std::meta reflection (GCC 15+ with -freflection)"
#endif

#include <fut/http/route.hpp>

namespace fut::refl {

namespace meta = std::meta;

constexpr std::size_t max_ns_members = 32;
constexpr std::size_t max_entity_annotations = 8;

// Scannable-annotated class types + nested namespaces of a namespace.
template <std::size_t N, meta::info Ns>
consteval std::array<meta::info, N> copy_scannable_members() {
    std::array<meta::info, N> out{};
    std::size_t i = 0;
    for (auto m : meta::members_of(Ns, meta::access_context::unchecked())) {
        if (i >= N) break;
        if (meta::is_type(m) && meta::is_class_type(m)) {
            for (auto a : meta::annotations_of(m))
                if (meta::is_base_of_type(^^fut::scannable, meta::type_of(a))) {
                    out[i++] = m;
                    break;
                }
        } else if (meta::is_namespace(m)) {
            out[i++] = m;   // recurse into nested namespaces
        }
    }
    return out;
}

template <meta::info Ann, meta::info Entity>
consteval std::size_t ann_operation_size() {
    using T = [: meta::type_of(Ann) :];
    return T::template operation<Entity>().size();
}

template <meta::info Ann, meta::info Entity>
constexpr auto ann_records_all() {
    using T = [: meta::type_of(Ann) :];
    return T::template operation<Entity>();
}

// copy scannable annotations into a constexpr array
template <std::size_t N, meta::info Entity>
consteval std::array<meta::info, N> copy_scannable_anns() {
    std::array<meta::info, N> out{};
    std::size_t i = 0;
    for (auto a : meta::annotations_of(Entity))
        if (i < N && meta::is_base_of_type(^^fut::scannable, meta::type_of(a)))
            out[i++] = a;
    return out;
}

template <meta::info Entity, std::size_t A>
consteval std::size_t entity_ann_size() {
    constexpr auto anns = copy_scannable_anns<max_entity_annotations, Entity>();
    if constexpr (A >= max_entity_annotations || anns[A] == meta::info{}) return 0;
    else return ann_operation_size<anns[A], Entity>();
}

template <meta::info Entity>
consteval std::size_t entity_route_count() {
    return [&]<std::size_t... A>(std::index_sequence<A...>) {
        return (entity_ann_size<Entity, A>() + ... + 0);
    }(std::make_index_sequence<max_entity_annotations>());
}

template <meta::info Ns>
consteval std::size_t ns_count();

template <meta::info Ns, std::size_t M>
consteval std::size_t member_route_count() {
    constexpr auto ms = copy_scannable_members<max_ns_members, Ns>();
    if constexpr (M >= max_ns_members || ms[M] == meta::info{}) return 0;
    else {
        constexpr meta::info m = ms[M];
        if constexpr (meta::is_namespace(m)) return ns_count<m>();
        else return entity_route_count<m>();
    }
}

template <meta::info Ns>
consteval std::size_t ns_count() {
    return [&]<std::size_t... M>(std::index_sequence<M...>) {
        return (member_route_count<Ns, M>() + ... + 0);
    }(std::make_index_sequence<max_ns_members>());
}

struct scan_hit {
    meta::info entity{};
    meta::info ann{};
    std::size_t k{};
};

template <meta::info Entity>
consteval scan_hit entity_locate(std::size_t idx) {
    scan_hit out{};
    [&]<std::size_t... A>(std::index_sequence<A...>) {
        bool done = false;
        (((void) [&] {
             if (done) return;
             constexpr auto anns = copy_scannable_anns<max_entity_annotations, Entity>();
             if constexpr (A < max_entity_annotations && anns[A] != meta::info{}) {
                 constexpr std::size_t sz = ann_operation_size<anns[A], Entity>();
                 if (sz == 0) return;
                 if (idx < sz) {
                     out = scan_hit{Entity, anns[A], idx};
                     done = true;
                 } else idx -= sz;
             }
         }()), ...);
    }(std::make_index_sequence<max_entity_annotations>());
    return out;
}

template <meta::info Ns>
consteval scan_hit ns_locate(std::size_t idx);

template <meta::info Ns, std::size_t M>
consteval scan_hit member_locate(std::size_t idx) {
    constexpr auto ms = copy_scannable_members<max_ns_members, Ns>();
    if constexpr (M >= max_ns_members || ms[M] == meta::info{}) return {};
    else {
        constexpr meta::info m = ms[M];
        if constexpr (meta::is_namespace(m)) return ns_locate<m>(idx);
        else return entity_locate<m>(idx);
    }
}

template <meta::info Ns>
consteval scan_hit ns_locate(std::size_t idx) {
    scan_hit out{};
    [&]<std::size_t... M>(std::index_sequence<M...>) {
        bool done = false;
        (((void) [&] {
             if (done) return;
             constexpr std::size_t sub = member_route_count<Ns, M>();
             if (sub == 0) return;
             if (idx < sub) {
                 out = member_locate<Ns, M>(idx);
                 done = true;
             } else idx -= sub;
         }()), ...);
    }(std::make_index_sequence<max_ns_members>());
    return out;
}

template <meta::info Ns, std::size_t I>
constexpr ::fut::route_record record_at() {
    constexpr scan_hit h = ns_locate<Ns>(I);
    constexpr auto rs = ann_records_all<h.ann, h.entity>();
    return rs[h.k];
}

template <meta::info Ns, std::size_t... I>
constexpr std::array<::fut::route_record, sizeof...(I)> ns_records_impl(
    std::index_sequence<I...>) {
    std::array<::fut::route_record, sizeof...(I)> out{};
    ((out[I] = record_at<Ns, I>()), ...);
    return out;
}

template <meta::info Ns>
constexpr auto ns_records_v =
    ns_records_impl<Ns>(std::make_index_sequence<ns_count<Ns>()>{});

// constexpr auto t = fut::refl::make_scanned_table<^^admin>("admin", "/api/admin");
template <meta::info Ns>
constexpr ::fut::app_table make_scanned_table(std::string_view name,
                                              std::string_view prefix) {
    return ::fut::app_table{name, prefix, ns_records_v<Ns>.data(), ns_count<Ns>()};
}

} // namespace fut::refl

#define FUT_EXPORT_APP_NS(NAME, PREFIX, NS)                                        \
    namespace {                                                                    \
    constexpr auto fut_app_table_v =                                               \
        ::fut::refl::make_scanned_table<(NS)>((NAME), (PREFIX));                   \
    }                                                                              \
    extern "C" {                                                                   \
    FUT_EXPORT std::uint32_t fut_abi_version() { return ::fut::abi_version; }      \
    FUT_EXPORT const ::fut::app_table* fut_app_table() {                           \
        return &fut_app_table_v;                                                    \
    }                                                                              \
    }
