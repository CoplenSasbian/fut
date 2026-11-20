#pragma once

// fut/annotate.hpp -- C++26 annotation types.
//
// http_action{path, method} is the verb base; http_get etc. derive from it
// and default the method enum. The controller annotation derives
// fut::scannable: the namespace scanner finds controller-annotated CLASS
// TYPES and calls controller::operation(class_info).
//
//     struct [[=fut::controller("/users")]] users_controller {
//         [[=fut::http_get("/hits")]]       // -> GET  /users/hits
//         asio::awaitable<hits_out> hits();
//     };
//
// NOTE: verbs are STRUCTS (not constexpr functions) because GCC 16.2's
// annotation system cannot resolve the type of a constexpr-function-call
// annotation value; struct-derived annotations work correctly.

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

#include <meta>

#include <fut/invoke.hpp>
#include <fut/route.hpp>

namespace fut {

inline constexpr std::size_t annot_path_max = 128;

// ---------------------------------------------------------------------------
// http_action: the verb base. {path, method_enum}.
// ---------------------------------------------------------------------------

struct http_action {
    detail::text_buf<annot_path_max> path{};
    std::uint8_t method_val{};   // fut::method enum value

    constexpr http_action() = default;
    constexpr http_action(std::string_view p, std::uint8_t m) : path(p), method_val(m) {}
};

struct http_get : http_action {
    constexpr http_get() : http_action({}, static_cast<std::uint8_t>(method::GET)) {}
    constexpr http_get(std::string_view p)
        : http_action(p, static_cast<std::uint8_t>(method::GET)) {}
};

struct http_post : http_action {
    constexpr http_post() : http_action({}, static_cast<std::uint8_t>(method::POST)) {}
    constexpr http_post(std::string_view p)
        : http_action(p, static_cast<std::uint8_t>(method::POST)) {}
};

struct http_put : http_action {
    constexpr http_put() : http_action({}, static_cast<std::uint8_t>(method::PUT)) {}
    constexpr http_put(std::string_view p)
        : http_action(p, static_cast<std::uint8_t>(method::PUT)) {}
};

struct http_delete : http_action {
    constexpr http_delete() : http_action({}, static_cast<std::uint8_t>(method::DEL)) {}
    constexpr http_delete(std::string_view p)
        : http_action(p, static_cast<std::uint8_t>(method::DEL)) {}
};

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

// The http_action-derived annotation info of a member.
template <std::size_t N, std::meta::info Member>
consteval std::array<std::meta::info, N> copy_verb_anns() {
    std::array<std::meta::info, N> out{};
    std::size_t i = 0;
    for (auto a : std::meta::annotations_of(Member))
        if (i < N && std::meta::is_base_of_type(^^fut::http_action, std::meta::type_of(a)))
            out[i++] = a;
    return out;
}

// The controller annotation on the class.
template <std::size_t N, std::meta::info Entity>
consteval std::array<std::meta::info, N> copy_self_anns() {
    std::array<std::meta::info, N> out{};
    std::size_t i = 0;
    for (auto a : std::meta::annotations_of(Entity))
        if (i < N && std::meta::is_base_of_type(^^fut::controller, std::meta::type_of(a)))
            out[i++] = a;
    return out;
}

// method enum -> HTTP method string (uppercase, standard form)
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
