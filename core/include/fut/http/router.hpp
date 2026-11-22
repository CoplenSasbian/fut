#pragma once

// fut/http/router.hpp -- the router module interface.
//
// The server executable implements it; app DLLs obtain it from the context
// and register their routes (and, optionally, their own middleware):
//
//     auto* router = fut::module_as<const fut::http::router_module>(ctx, "router");
//     router->add_route(router->self, fut::method::GET, "/api/admin/ping", &fn);
//
// Middleware is a coroutine that either answers the request or calls next():
//
//     asio::awaitable<fut::response> timing(
//         const fut::request& req, const fut::path_match& params,
//         const fut::http::next_fn& next)
//     {
//         auto t0 = std::chrono::steady_clock::now();
//         fut::response res = co_await next(req, params);
//         res.headers.emplace_back("x-ms", std::to_string(elapsed(t0)));
//         co_return res;
//     }
//
// Two scopes exist, and an app can only reach its own:
//
//   platform  registered by the executable (fut::exec::server::add_middleware),
//             runs for every request, including the ones that match nothing;
//   app       registered through add_middleware below while the app loads,
//             runs for that app's routes only.
//
// An app's middleware never affects another app: the router keeps them in
// separate chains, because apps are independent deployment units.
//
// The interface is plain function pointers: no virtuals, no shared library
// runtime. It crosses the DLL boundary, so both sides must be built from
// these headers with the same compiler.

#include <cstddef>
#include <functional>
#include <string_view>

#include <boost/asio/awaitable.hpp>

#include <fut/ctx.hpp>
#include <fut/http/route.hpp>
#include <fut/http/stream.hpp>

namespace fut::http {

// The ABI of one action. Both sides (executable and app DLL) must agree,
// which is why every module is built from the same fut headers.
using route_fn = boost::asio::awaitable<response> (*)(const request&, const path_match&);

// Resumes the rest of the chain.
using next_fn = std::function<boost::asio::awaitable<response>(const request&,
                                                               const path_match&)>;

// One middleware: answer the request, or delegate through next().
using middleware_fn = std::function<boost::asio::awaitable<response>(
    const request&, const path_match&, const next_fn&)>;

// What an app registers: a plain function pointer, deliberately not a
// std::function. The callable would otherwise be constructed inside the DLL
// and destroyed after it is unloaded, which frees through a dangling vtable;
// a pointer stays valid and the executable wraps it in its own std::function.
using middleware_hook = boost::asio::awaitable<response> (*)(
    const request&, const path_match&, const next_fn&);

struct router_module {
    // Server-private router object; handed back to the functions unchanged.
    void* self = nullptr;

    // Register one route. pattern is the complete path (app prefix already
    // composed in by the registering annotation); the router copies it.
    void (*add_route)(void* self, method m, std::string_view pattern, route_fn fn) = nullptr;

    // Register a middleware for the app currently being loaded. Every route
    // that app registers runs through it; nothing else does.
    void (*add_middleware)(void* self, middleware_hook fn) = nullptr;
};

// Convenience: the router module of a context, or nullptr. The type is the
// key; "router" is only its name (and today the only module, so either form
// works).
[[nodiscard]] inline const router_module* router_of(const fut_ctx* ctx) {
    return ctx ? ctx->get_module<const router_module>("router") : nullptr;
}

} // namespace fut::http
