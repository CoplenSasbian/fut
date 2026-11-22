#pragma once

// fut/http/filter.hpp -- fut::filter: register an app middleware by annotation.
//
//     namespace demo {
//     [[=fut::filter{}]]
//     asio::awaitable<fut::response> stamp(const fut::request& req,
//                                          const fut::path_match& params,
//                                          const fut::http::next_fn& next) { ... }
//     }
//
// The scanner visits the annotated function while the app registers and hands
// it to router_module::add_middleware, so an app never writes the registration
// boilerplate -- and never has to reach for fut::scannable for this.
//
// Scope is the app being loaded. Order is declaration order: the first filter
// declared is the outermost, so it sees the response after the inner ones --
// including a response an inner filter produced by short-circuiting.
//
// The signature is enforced at compile time (the conversion below plus the
// static_asserts), so a typo in a parameter type fails the build instead of
// silently registering nothing.

#include <boost/asio/awaitable.hpp>

#include <meta>

#include <fut/ctx.hpp>
#include <fut/http/router.hpp>
#include <fut/scannable.hpp>

namespace fut {

struct filter : scannable {
    template <std::meta::info Entity>
    void operation(fut_ctx* ctx) const {
        static_assert(std::meta::is_function(Entity),
                      "fut::filter must annotate a free function with the middleware "
                      "signature: asio::awaitable<fut::response>(const fut::request&, "
                      "const fut::path_match&, const fut::http::next_fn&)");

        // NB: an alias first -- '<:' right after a template argument list start
        // would be read as the '[' digraph.
        using return_type = [: std::meta::return_type_of(Entity) :];
        static_assert(std::is_same_v<return_type, boost::asio::awaitable<fut::response>>,
                      "a fut::filter function must return boost::asio::awaitable<fut::response>");

        // Exact match required: a wrong parameter type fails here, with the
        // compiler naming the expected signature.
        constexpr http::middleware_hook hook = static_cast<http::middleware_hook>(&[: Entity :]);

        const http::router_module* rm = http::router_of(ctx);
        if (rm != nullptr && rm->add_middleware != nullptr) rm->add_middleware(rm->self, hook);
    }
};

} // namespace fut
