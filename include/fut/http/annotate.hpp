#pragma once

// fut/http/annotate.hpp -- C++26 annotation types.
//
// http_action{path, method} is the verb data; http_get etc. are constexpr
// functions returning http_action values. GCC records the annotation type
// as "const http_action" (cv-qualified), so detection compares against
// ^^const http_action. The controller annotation derives fut::scannable:
// the namespace scanner finds controller-annotated CLASS TYPES and calls
// controller::operation(class_info).

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

#include <meta>

#include <fut/http/invoke.hpp>
#include <fut/http/route.hpp>
#include <fut/scannable.hpp>

namespace fut {

inline constexpr std::size_t annot_path_max = 128;

// ---------------------------------------------------------------------------
// http_action: the verb data type. {path, method_enum}.
// http_get("/ping") is a constexpr function returning http_action.
// ---------------------------------------------------------------------------

struct http_action {
    detail::text_buf<annot_path_max> path{};
    std::uint8_t method_val{};   // fut::method enum value

    constexpr http_action() = default;
    constexpr http_action(std::string_view p, std::uint8_t m) : path(p), method_val(m) {}
};

// constexpr factories (used as annotation values)
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

    template <std::meta::info Entity>
    static constexpr auto operation();
};

// ---- controller internals: walk class members for http_action annotations ----
// GCC records constexpr-function annotation types as cv-qualified,
// so detection compares against ^^const http_action.

namespace ctrl {

constexpr std::size_t max_members = 16;

template <std::meta::info Entity>
consteval std::array<std::meta::info, max_members> copy_members() {
    std::array<std::meta::info, max_members> out{};
    std::size_t i = 0;
    for (auto m : std::meta::members_of(Entity, std::meta::access_context::unchecked()))
        if (std::meta::is_class_member(m) && std::meta::is_function(m) && i < max_members)
            out[i++] = m;
    return out;
}

// The http_action annotation of a member.
// GCC records constexpr-function annotation types as cv-qualified, so the
// filter target is ^^const http_action. annotations_of_with_type does the
// comparison; no manual type_of iteration needed.
template <std::size_t N, std::meta::info Member>
consteval std::array<std::meta::info, N> copy_verb_anns() {
    std::array<std::meta::info, N> out{};
    std::size_t i = 0;
    for (auto a : std::meta::annotations_of_with_type(Member, ^^const fut::http_action))
        if (i < N) out[i++] = a;
    return out;
}

// The controller annotation on the class (struct annotation, no const issue).
template <std::size_t N, std::meta::info Entity>
consteval std::array<std::meta::info, N> copy_self_anns() {
    std::array<std::meta::info, N> out{};
    std::size_t i = 0;
    for (auto a : std::meta::annotations_of(Entity)) {
        if (i < N && std::meta::is_base_of_type(^^fut::controller, std::meta::type_of(a)))
            out[i++] = a;
    }
    return out;
}

// method enum -> HTTP string
consteval std::string_view method_to_string(std::uint8_t val) {
    switch (static_cast<fut::method>(val)) {
    case fut::method::GET:    return "GET";
    case fut::method::POST:   return "POST";
    case fut::method::PUT:    return "PUT";
    case fut::method::DEL:    return "DELETE";
    }
    return "UNKNOWN";
}

template <std::meta::info Entity, std::size_t M>
consteval bool member_is_route() {
    constexpr auto ms = copy_members<Entity>();
    if constexpr (M >= max_members || ms[M] == std::meta::info{}) return false;
    else {
        constexpr std::meta::info member = ms[M];
        constexpr auto verbs = copy_verb_anns<1, member>();
        return verbs[0] != std::meta::info{};
    }
}

template <std::meta::info Entity>
consteval std::size_t route_count() {
    return [&]<std::size_t... M>(std::index_sequence<M...>) {
        return ((member_is_route<Entity, M>() ? 1 : 0) + ... + 0);
    }(std::make_index_sequence<max_members>());
}

template <std::meta::info Entity, std::size_t M>
constexpr ::fut::route_record member_record() {
    constexpr auto ms = copy_members<Entity>();
    constexpr std::meta::info member = ms[M];

    constexpr auto verbs = copy_verb_anns<1, member>();
    constexpr auto verb = [: std::meta::constant_of(verbs[0]) :];

    constexpr auto self = copy_self_anns<1, Entity>();
    constexpr auto ctl = [: std::meta::constant_of(self[0]) :];

    return ::fut::route_record{
        method_to_string(verb.method_val),
        ::fut::refl::join_pattern(ctl.path.view(), verb.path.view()),
        &::fut::refl::invoke_member_route<Entity, member>};
}

} // namespace ctrl

template <std::meta::info Entity>
constexpr auto controller::operation() {
    constexpr std::size_t n = ctrl::route_count<Entity>();
    std::array<::fut::route_record, n> out{};
    std::size_t off = 0;
    [&]<std::size_t... M>(std::index_sequence<M...>) {
        (([&] {
             if constexpr (ctrl::member_is_route<Entity, M>()) {
                 out[off] = ctrl::member_record<Entity, M>();
                 ++off;
             }
         }()), ...);
    }(std::make_index_sequence<ctrl::max_members>());
    return out;
}

} // namespace fut
