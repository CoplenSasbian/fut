#pragma once

// Fluent builder for the whole server: everything an embedder may want to
// tweak (port, worker threads, app directory, timeouts, config file, extra
// in-process routes) with sensible encapsulated defaults. Explicitly set
// values override the JSON config file; anything unset falls back to
// config.json and then to built-in defaults.
//
//   int main() {
//       return fut::exec::server_builder()
//           .config_file("config.json")
//           .port(8080)
//           .threads(4)
//           .add_route(fut::method::GET, "/health", &health)
//           .run();
//   }

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <fut/http/router.hpp>
#include <fut/log.hpp>

namespace fut::exec {

class server_builder {
public:
    server_builder& config_file(std::filesystem::path p) { cfg_file_ = std::move(p); return *this; }
    server_builder& host(std::string_view v) { host_ = std::string(v); return *this; }
    server_builder& port(unsigned short v) { port_ = v; return *this; }
    server_builder& threads(int v) { threads_ = v; return *this; }
    server_builder& apps_dir(std::string_view v) { apps_dir_ = std::string(v); return *this; }
    server_builder& request_timeout(std::chrono::seconds v) { timeout_ = v; return *this; }

    /// "trace" / "debug" / "info" / "warning" / "error" / "critical" / "off"
    /// (matched against the enum through reflection); overrides config.json.
    server_builder& log_level(std::string_view v) { log_level_ = std::string(v); return *this; }

    // ---- logging chains ------------------------------------------------
    // Chains are created when run() starts; the embedder owns the policy.
    // The default set is a colour console at info plus a rotating file at
    // trace. Rebuild it with clear_log_chains() + add_log_chain(), or use
    // one of the presets below (each preset clears what was there).

    server_builder& clear_log_chains() { chains_.clear(); return *this; }

    server_builder& add_log_chain(fut::log::chain_options chain) {
        chains_.push_back(std::move(chain));
        return *this;
    }

    /// Clear the chains and log to stdout only.
    server_builder& log_to_console(bool color = true,
                                   fut::log::level minimum = fut::log::level::info);

    /// Clear the chains and log to a rotating file only.
    server_builder& log_to_file(std::filesystem::path path,
                                std::uint64_t max_bytes = std::uint64_t{2} << 20,
                                std::uint32_t max_files = 3,
                                fut::log::level minimum = fut::log::level::trace);

    /// The stock set: colour console at info + rotating file at trace.
    [[nodiscard]] static std::vector<fut::log::chain_options> default_log_chains();

    // Extra in-process routes, registered without any DLL. They belong to
    // the built-in "core" app alongside /api/core/status.
    server_builder& add_route(fut::method m, std::string_view pattern,
                              fut::http::route_fn fn) {
        extra_routes_.push_back(extra_route{m, std::string(pattern), fn});
        return *this;
    }

    // Loads apps, serves until SIGINT/SIGTERM, returns a process exit code.
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
    std::vector<fut::log::chain_options> chains_{default_log_chains()};
};

} // namespace fut::exec
