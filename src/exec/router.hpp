#pragma once

// The server's route registry. It implements fut::http::router_module, so
// app DLLs register into it through the context; the executable can also
// add routes directly (builder.add_route / the built-in core app).
//
// Patterns are complete paths: the registering DLL composes its add_app()
// prefix itself, so matching is a straight per-entry pattern match.

#include <cstddef>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <fut/http/router.hpp>

namespace fut::exec {

class router {
public:
    struct entry {
        fut::method method = fut::method::GET;
        std::string pattern{};
        fut::http::route_fn invoke = nullptr;
        std::string app{};
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

    // Every route registered between begin_app/end_app belongs to that app.
    void begin_app(std::string_view name, std::string_view prefix);
    void end_app();

    void add_route(fut::method m, std::string_view pattern, fut::http::route_fn fn);

    // The most specific match (most literal segments) wins; registration
    // order breaks ties.
    [[nodiscard]] std::optional<hit> match(fut::method m, std::string_view path) const;

    // The interface handed to app DLLs through the context.
    [[nodiscard]] const fut::http::router_module* module() const { return &module_; }

    [[nodiscard]] std::span<const entry> entries() const { return entries_; }
    [[nodiscard]] std::span<const app_info> apps() const { return apps_; }

    router() = default;
    router(const router&) = delete;
    router& operator=(const router&) = delete;

private:
    static constexpr std::size_t npos = static_cast<std::size_t>(-1);

    static void add_route_thunk(void* self, fut::method m, std::string_view pattern,
                                fut::http::route_fn fn);

    fut::http::router_module module_{this, &add_route_thunk};
    std::vector<entry> entries_{};
    std::vector<app_info> apps_{};
    std::size_t current_app_ = npos;
};

} // namespace fut::exec
