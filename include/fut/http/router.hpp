#pragma once

// fut/http/router.hpp -- the router module interface.
//
// The server executable implements it; app DLLs obtain it from the context
// and register their routes:
//
//     auto* router = fut::module_as<const fut::http::router_module>(ctx, "router");
//     router->add_route(router->self, fut::method::GET, "/api/admin/ping", &fn);
//
// The interface is plain function pointers: no virtuals, no shared library
// runtime, and the layout is fixed by this header.

#include <string_view>

#include <boost/asio/awaitable.hpp>

#include <fut/ctx.hpp>
#include <fut/http/route.hpp>

namespace fut::http {

// The ABI of one action. Both sides (executable and app DLL) must agree,
// which is why every module is built from the same fut headers.
using route_fn = boost::asio::awaitable<response> (*)(const request&, const path_match&);

struct router_module {
    // Server-private router object; handed back to add_route unchanged.
    void* self = nullptr;

    // Register one route. `pattern` is the complete path (app prefix
    // already composed in by the registering annotation); the router copies
    // it, so the string need not outlive the call.
    void (*add_route)(void* self, method m, std::string_view pattern, route_fn fn) = nullptr;
};

// Convenience: the router module of a context, or nullptr.
[[nodiscard]] inline const router_module* router_of(const fut_ctx* ctx) {
    return module_as<const router_module>(ctx, "router");
}

} // namespace fut::http
