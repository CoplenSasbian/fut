#pragma once

// fut/http/annotate.hpp -- the HTTP annotations.
//
// http_action{path, method} is the verb data; http_get() etc. are constexpr
// functions returning http_action values. GCC records the annotation type of
// such a value as "const http_action", so detection filters on ^^const
// http_action.
//
// fut::controller derives fut::scannable: the global scanner (fut/scan.hpp)
// finds controller-annotated CLASS TYPES and calls
//
//     controller::operation<TheClass>(ctx, prefix)
//
// which walks the class's member functions, reads each verb annotation, and
// registers one route per method with the router module. The route path is
// composed from three parts:
//
//     add_app(admin /api/admin ...)  ->  "/api/admin"   (compile-time macro)
//     [[=fut::controller("/users")]] ->  "/users"       (class annotation)
//     [[=fut::http_get("/{id}")]]    ->  "/{id}"        (method annotation)
//
//     => "/api/admin/users/{id}"
//
// Parameter binding (fut/http/bind.hpp) is read by the invoke machinery
// while building arguments; the scanner never sees it.

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>

#include <meta>

#include <fut/http/invoke.hpp>
#include <fut/http/route.hpp>
#include <fut/http/router.hpp>
#include <fut/scannable.hpp>

namespace fut {

inline constexpr std::size_t annot_path_max = 128;

// ---------------------------------------------------------------------------
// http_action: the verb data type. {path, method}.
// http_get("/ping") is a constexpr function returning http_action.
// ---------------------------------------------------------------------------

struct http_action {
    detail::text_buf<annot_path_max> path{};
    std::uint8_t method_val{};   // fut::method enum value

    constexpr http_action() = default;
    constexpr http_action(std::string_view p, std::uint8_t m) : path(p), method_val(m) {}
};

constexpr http_action http_get(std::string_view path) {
    return {path, static_cast<std::uint8_t>(method::GET)};
}
constexpr http_action http_post(std::string_view path) {
    return {path, static_cast<std::uint8_t>(method::POST)};
}
constexpr http_action http_put(std::string_view path) {
    return {path, static_cast<std::uint8_t>(method::PUT)};
}
constexpr http_action http_delete(std::string_view path) {
    return {path, static_cast<std::uint8_t>(method::DEL)};
}

// ---------------------------------------------------------------------------
// controller: the scannable annotation.
// ---------------------------------------------------------------------------

struct controller : scannable {
    detail::text_buf<annot_path_max> path{};
    constexpr controller() = default;
    constexpr controller(std::string_view s) : path(s) {}

    // The scan contract. Walks the members of `Entity`, registers one route
    // per verb-annotated member function.
    template <std::meta::info Entity>
    static void operation(::fut::fut_ctx* ctx, std::string_view prefix);
};

// ---- controller internals -------------------------------------------------

namespace ctrl {

constexpr std::size_t max_members = 16;

template <std::meta::info Entity>
consteval std::array<std::meta::info, max_members> members() {
    std::array<std::meta::info, max_members> out{};
    std::size_t i = 0;
    for (auto m : std::meta::members_of(Entity, std::meta::access_context::unchecked()))
        if (std::meta::is_class_member(m) && std::meta::is_function(m) && i < max_members)
            out[i++] = m;
    return out;
}

// The http_action annotation of a member. GCC records constexpr-function
// annotation types as cv-qualified, hence ^^const http_action.
template <std::size_t N, std::meta::info Member>
consteval std::array<std::meta::info, N> verb_anns() {
    std::array<std::meta::info, N> out{};
    std::size_t i = 0;
    for (auto a : std::meta::annotations_of_with_type(Member, ^^const fut::http_action))
        if (i < N) out[i++] = a;
    return out;
}

// The controller annotation of the class (a struct value, not cv-qualified).
template <std::size_t N, std::meta::info Entity>
consteval std::array<std::meta::info, N> self_anns() {
    std::array<std::meta::info, N> out{};
    std::size_t i = 0;
    for (auto a : std::meta::annotations_of(Entity))
        if (i < N && std::meta::is_base_of_type(^^fut::controller, std::meta::type_of(a)))
            out[i++] = a;
    return out;
}

template <std::meta::info Entity, std::size_t M>
void register_member(const ::fut::http::router_module* rm, std::string_view prefix) {
    constexpr auto ms = members<Entity>();
    if constexpr (M < max_members && ms[M] != std::meta::info{}) {
        constexpr std::meta::info member = ms[M];
        constexpr auto verbs = verb_anns<1, member>();
        if constexpr (verbs[0] != std::meta::info{}) {
            constexpr auto verb = [: std::meta::constant_of(verbs[0]) :];

            constexpr auto selfs = self_anns<1, Entity>();
            constexpr auto ctl = [: std::meta::constant_of(selfs[0]) :];

            const std::string pattern =
                ::fut::join_path(prefix, ctl.path.view(), verb.path.view());
            rm->add_route(rm->self, static_cast<::fut::method>(verb.method_val), pattern,
                          &::fut::refl::invoke_member_route<Entity, member>);
        }
    }
}

} // namespace ctrl

template <std::meta::info Entity>
void controller::operation(::fut::fut_ctx* ctx, std::string_view prefix) {
    static_assert(std::meta::is_type(Entity) && std::meta::is_class_type(Entity),
                  "fut::controller must annotate a class type -- the scanner hands it "
                  "whatever carries the annotation, and a controller reads class members");
    const ::fut::http::router_module* rm = ::fut::http::router_of(ctx);
    if (!rm || !rm->add_route) return;
    [&]<std::size_t... M>(std::index_sequence<M...>) {
        (ctrl::register_member<Entity, M>(rm, prefix), ...);
    }(std::make_index_sequence<ctrl::max_members>());
}

} // namespace fut
