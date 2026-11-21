#pragma once

// The server: one object that is configured and then run.
//
//   int main() {
//       return fut::exec::server()
//           .config_file("config.json")
//           .port(8080)
//           .threads(4)
//           .add_route(fut::method::GET, "/health", &health)
//           .run();          // loads apps, serves until SIGINT/SIGTERM
//   }
//
// Every setter returns the server, so a configuration reads as one
// expression; run() blocks and returns a process exit code. Explicitly set
// values override the JSON config file, which overrides the built-in
// defaults.

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <fut/http/error.hpp>
#include <fut/http/router.hpp>
#include <fut/log.hpp>
#include <typeinfo>

#include "module_registry.hpp"

namespace fut::exec {

class server {
public:
    server& config_file(std::filesystem::path p) { cfg_file_ = std::move(p); return *this; }
    server& host(std::string_view v) { host_ = std::string(v); return *this; }
    server& port(unsigned short v) { port_ = v; return *this; }
    server& threads(int v) { threads_ = v; return *this; }
    server& apps_dir(std::string_view v) { apps_dir_ = std::string(v); return *this; }
    server& request_timeout(std::chrono::seconds v) { timeout_ = v; return *this; }

    /// "trace" / "debug" / "info" / "warning" / "error" / "critical" / "off"
    /// (matched against the enum through reflection); overrides config.json.
    server& log_level(std::string_view v) { log_level_ = std::string(v); return *this; }

    // ---- logging chains ------------------------------------------------
    // Chains are created when run() starts; the embedder owns the policy.
    // The default set is a colour console at info plus a rotating file at
    // trace. Rebuild it with clear_log_chains() + add_log_chain(), or use
    // one of the presets below (each preset clears what was there).

    // Every parameter is caller data: the pattern, the file, the sizes and
    // the levels all come from here or from config.json -- never from
    // constants inside the server.

    server& clear_log_chains() { chains_.clear(); chains_explicit_ = true; return *this; }

    server& add_log_chain(fut::log::chain_options chain) {
        chains_.push_back(std::move(chain));
        chains_explicit_ = true;
        return *this;
    }

    /// Clear the chains and log to stdout only.
    server& log_to_console(
        bool color = true, fut::log::level minimum = fut::log::level::info,
        std::string_view pattern = fut::log::console_pattern);

    /// Clear the chains and log to a rotating file only.
    server& log_to_file(
        std::filesystem::path path, std::uint64_t max_bytes = std::uint64_t{2} << 20,
        std::uint32_t max_files = 3, fut::log::level minimum = fut::log::level::trace,
        std::string_view pattern = fut::log::file_pattern);

    /// The stock set (colour console at info + rotating file at trace) with
    /// caller-supplied patterns and file settings.
    [[nodiscard]] static std::vector<fut::log::chain_options> default_log_chains(
        std::string_view console_pattern = fut::log::console_pattern,
        std::string_view file_pattern = fut::log::file_pattern,
        std::filesystem::path file_path = "logs/fut.log",
        std::uint64_t max_bytes = std::uint64_t{2} << 20, std::uint32_t max_files = 3,
        fut::log::level console_min = fut::log::level::info,
        fut::log::level file_min = fut::log::level::trace);

    // Extra in-process routes, registered without any DLL. They belong to
    // the built-in "core" app alongside /api/core/status.
    server& add_route(fut::method m, std::string_view pattern,
                              fut::http::route_fn fn) {
        extra_routes_.push_back(extra_route{m, std::string(pattern), fn});
        return *this;
    }

    // Publish one more service to app DLLs, reachable through
    // ctx->get_module<T>() / ctx->get_module<T>("name"). The instance must
    // outlive the server. Register modules that share a type under distinct
    // names -- a type-only lookup of an ambiguous type is an error.
    server& add_module(std::string_view name, std::string_view type_name,
                               void* module) {
        modules_.add(name, type_name, module);
        return *this;
    }

    template <class T>
    server& add_module(std::string_view name, T* module) {
        return add_module(name, typeid(T).name(), module);
    }

    // Platform error formatting: replaces the body written for every error
    // status (default {"error":"<message>"}). Returning a response from here
    // is how an operator standardises error envelopes across all apps.
    server& on_error(std::function<fut::response(unsigned, std::string_view)> handler) {
        error_handler_ = std::move(handler);
        return *this;
    }

    // Platform middleware: runs for every request of every app, including
    // requests that match no route. Registration order = outer to inner.
    // An app can only add middleware to its own routes (see
    // fut::http::router_module::add_middleware).
    server& add_middleware(fut::http::middleware_fn mw) {
        middlewares_.push_back(std::move(mw));
        return *this;
    }

    // Builds the pipeline (apps, routes, chains), serves until SIGINT/SIGTERM
    // and returns a process exit code.
    int run();

private:
    struct extra_route {
        fut::method method = fut::method::GET;
        std::string pattern{};
        fut::http::route_fn invoke = nullptr;
    };

    std::filesystem::path cfg_file_{"config.json"};
    std::optional<std::string> host_;
    std::optional<unsigned short> port_;
    std::optional<int> threads_;
    std::optional<std::string> apps_dir_;
    std::optional<std::chrono::seconds> timeout_;
    std::optional<std::string> log_level_;
    std::vector<extra_route> extra_routes_;
    std::vector<fut::http::middleware_fn> middlewares_;
    module_registry modules_{};
    std::function<fut::response(unsigned, std::string_view)> error_handler_{};
    std::vector<fut::log::chain_options> chains_{default_log_chains()};

    // True once the embedder touched the chains: an explicit builder policy
    // wins over config.json, which wins over the compiled-in defaults.
    bool chains_explicit_ = false;
};

} // namespace fut::exec
