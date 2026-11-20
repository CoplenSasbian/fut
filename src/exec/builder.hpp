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
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
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
};

} // namespace fut::exec
