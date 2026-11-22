#pragma once

// The server's route registry and dispatch pipeline.
//
// Two middleware scopes, deliberately separate:
//
//   platform  registered by the executable; wraps the whole dispatch, so it
//             also sees requests that match no route (404), which is what
//             logging, metrics, CORS and global authentication need;
//   app       registered by one app DLL while it loads; wraps only that
//             app's routes. Apps are independent deployment units, so one
//             app can never change another app's behaviour.
//
// Order: platform middleware (outermost, registration order) -> matching ->
// app middleware (registration order) -> the route handler.

#include <cstddef>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include <boost/asio/awaitable.hpp>

#include <fut/http/router.hpp>

#include <fut/export.hpp>

namespace fut::exec {

// A route's final handler: app middleware already wrapped around the route.
using handler_fn = std::function<boost::asio::awaitable<void>(
    const fut::request&, const fut::path_match&, fut::response&)>;

class FUT_API router {
public:
    struct entry {
        fut::method method = fut::method::GET;
        std::string pattern{};
        fut::http::route_fn invoke = nullptr;
        std::string app{};
        handler_fn handler{};   // built by finalize()
    };

    struct app_info {
        std::string name{};
        std::string prefix{};
        std::size_t routes = 0;
    };

    struct hit {
        const entry* route = nullptr;
        fut::path_match params{};
    };

    // Every route registered between begin_app/end_app belongs to that app,
    // and so does every middleware registered in the same window.
    void begin_app(std::string_view name, std::string_view prefix);
    void end_app();

    void add_route(fut::method m, std::string_view pattern, fut::http::route_fn fn);

    // App middleware arrives as a function pointer (see middleware_hook): the
    // executable owns the std::function wrappers it builds from it.
    // Smaller order runs earlier (outer). Ties keep registration order. The
    // chains are sorted once in finalize(); smaller order runs first.
    void add_middleware(fut::http::middleware_hook fn, int order = 0);

    // Platform middleware: runs for every request, matched or not.
    void add_global_middleware(fut::http::middleware_fn fn, int order = 0);

    // Wraps every route with its chains; call once after all apps are loaded.
    // Until then dispatch() falls back to the unwrapped route functions.
    void finalize();

    // Full pipeline for one request: platform chain -> match -> app chain ->
    // handler, with the error mapping described in fut/http/error.hpp.
    boost::asio::awaitable<void> dispatch(const fut::request& req, fut::response& out) const;


    // Replaces the body written for every error status. The default is
    // fut::error(), i.e. {"error":"<message>"}. Install it from the
    // executable (fut::exec::server::on_error) -- it is platform policy, not an
    // app's.
    void set_error_handler(std::function<fut::response(unsigned, std::string_view)> handler) {
        error_handler_ = std::move(handler);
    }

    // The most specific match (most literal segments) wins; registration
    // order breaks ties.
    [[nodiscard]] std::optional<hit> match(fut::method m, std::string_view path) const;

    // The interface handed to app DLLs through the context.
    [[nodiscard]] const fut::http::router_module* module() const { return &module_; }

    [[nodiscard]] std::span<const entry> entries() const { return entries_; }
    [[nodiscard]] std::span<const app_info> apps() const { return apps_; }
    // The platform chain, in run order.
    [[nodiscard]] std::vector<fut::http::middleware_fn> global_middleware() const {
        std::vector<fut::http::middleware_fn> out;
        out.reserve(global_.size());
        for (const auto& e : global_) out.push_back(e.fn);
        return out;
    }

    router() = default;
    router(const router&) = delete;
    router& operator=(const router&) = delete;

private:
    static constexpr std::size_t npos = static_cast<std::size_t>(-1);

    static void add_route_thunk(void* self, fut::method m, std::string_view pattern,
                                fut::http::route_fn fn);
    static void add_middleware_thunk(void* self, fut::http::middleware_hook fn, int order);

    boost::asio::awaitable<void> dispatch_chain(const fut::request& req,
                                                fut::response& out) const;
    boost::asio::awaitable<void> dispatch_matched(const fut::request& req,
                                                  fut::response& out) const;
    void make_error(fut::response& out, unsigned status, std::string_view message) const;

    fut::http::router_module module_{this, &add_route_thunk, &add_middleware_thunk};
    std::vector<entry> entries_{};
    std::vector<app_info> apps_{};
    // Chain entries carry their order; finalize() sorts each chain once, so a
    // request walks a plain vector.
    struct chain_entry {
        int order = 0;
        fut::http::middleware_fn fn{};
    };
    static bool by_order(const chain_entry& a, const chain_entry& b) {
        return a.order < b.order;   // stable_sort: ties keep registration order
    }

    std::vector<chain_entry> global_{};
    std::unordered_map<std::string, std::vector<chain_entry>> app_chains_{};
    std::size_t current_app_ = npos;
    std::function<fut::response(unsigned, std::string_view)> error_handler_{};
};

} // namespace fut::exec
