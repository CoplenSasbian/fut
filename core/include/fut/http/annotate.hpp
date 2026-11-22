#pragma once

// fut/http/annotate.hpp -- the HTTP annotations.
//
// http_action{path, method} is the verb data; http_get() etc. are constexpr
// functions returning http_action values. Verb detection goes through
// fut::ann::of<http_action>(), which is cv-agnostic (GCC records such a value
// as "const http_action", which callers never see).
//
// fut::controller derives fut::scannable: the global scanner (fut/scan.hpp)
// finds controller-annotated CLASS TYPES, materialises the annotation value
// and calls its member
//
//     annotation.template operation<TheClass>(ctx)
//
// which walks the class's member functions, reads each verb annotation, and
// registers one route per method with the router module. The path is
// composed from three parts:
//
//     ctx->app_prefix                ->  "/api/admin"   (app identity)
//     [[=fut::controller("/users")]] ->  "/users"       (the annotation)
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

#include <meta>

#include <fut/annotations.hpp>
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
constexpr http_action http_head(std::string_view path) {
    return {path, static_cast<std::uint8_t>(method::HEAD)};
}
constexpr http_action http_patch(std::string_view path) {
    return {path, static_cast<std::uint8_t>(method::PATCH)};
}
constexpr http_action http_options(std::string_view path) {
    return {path, static_cast<std::uint8_t>(method::OPTIONS)};
}
constexpr http_action http_trace(std::string_view path) {
    return {path, static_cast<std::uint8_t>(method::TRACE)};
}
constexpr http_action http_connect(std::string_view path) {
    return {path, static_cast<std::uint8_t>(method::CONNECT)};
}

// ---------------------------------------------------------------------------
// controller: the scannable annotation.
// ---------------------------------------------------------------------------

struct controller : scannable {
    detail::text_buf<annot_path_max> path{};
    constexpr controller() = default;
    constexpr controller(std::string_view s) : path(s) {}

    // The scan contract (a member function: `this` is the annotation's own
    // value, so the class path is this->path). Registers one route per
    // verb-annotated member function of `Entity`.
    template <std::meta::info Entity>
    void operation(::fut::fut_ctx* ctx) const {
        static_assert(std::meta::is_type(Entity) && std::meta::is_class_type(Entity),
                      "fut::controller must annotate a class type -- the scanner hands it "
                      "whatever carries the annotation, and a controller reads class members");

        const ::fut::http::router_module* rm = ::fut::http::router_of(ctx);
        if (!rm || !rm->add_route) return;

        const std::string_view app_prefix = ctx ? ctx->app_prefix : std::string_view{};
        const std::string_view class_path = this->path.view();

        template for (constexpr std::meta::info member :
                      std::define_static_array(std::meta::members_of(
                          Entity, std::meta::access_context::unchecked()))) {
            if constexpr (std::meta::is_class_member(member) &&
                          std::meta::is_function(member)) {
                // One route per http_action annotation on this method.
                template for (constexpr std::meta::info verb_ann :
                              std::define_static_array(::fut::ann::of<http_action>(member))) {
                    constexpr auto verb = [: std::meta::constant_of(verb_ann) :];
                    const std::string pattern =
                        ::fut::join_path(app_prefix, class_path, verb.path.view());
                    rm->add_route(rm->self, static_cast<::fut::method>(verb.method_val),
                                  pattern, &::fut::refl::invoke_member_route<Entity, member>);
                }
            }
        }
    }
};

} // namespace fut
