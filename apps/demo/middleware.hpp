#pragma once

// App-scoped middleware.
//
// A middleware is a coroutine that either answers the request or continues the
// chain. This one continues and rewrites the response, stamping the app's own
// routes. It is registered by an annotation on a plain class: the scanner
// hands every scannable annotation to its operation().
//
// Scope: this runs for the routes of THIS app only, after the executable's
// platform middleware. A 404 belongs to no app, so it does not run at all.

#include "common.hpp"

namespace demo {

inline asio::awaitable<fut::response> stamp(const fut::request& req,
                                           const fut::path_match& params,
                                           const fut::http::next_fn& next) {
    fut::response res = co_await next(req, params);
    res.headers.emplace_back("X-Demo-App", "yes");
    co_return res;
}

struct filter : fut::scannable {
    template <std::meta::info Entity>
    void operation(fut::fut_ctx* ctx) const {
        const auto* rm = ctx->get_module<fut::http::router_module>("router");
        if (rm && rm->add_middleware) rm->add_middleware(rm->self, &stamp);
    }
};

struct [[=filter{}]] register_filter;

} // namespace demo
