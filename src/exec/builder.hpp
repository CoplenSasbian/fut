#pragma once

// Fluent builder for the whole server: everything an embedder may want to
// tweak (port, worker threads, app directory, timeouts, config file) with
// sensible encapsulated defaults. Explicitly set values override the JSON
// config file; anything unset falls back to config.json and then to
// built-in defaults.

//   int main() {
//       return fut::exec::server_builder()
//           .config_file("config.json")
//           .port(8080)
//           .threads(4)
//           .run();
//   }

#include <chrono>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <fut/route.hpp>

namespace fut::exec {

class server_builder {
public:
    server_builder& config_file(std::filesystem::path p) { cfg_file_ = std::move(p); return *this; }
    server_builder& host(std::string_view v) { host_ = std::string(v); return *this; }
    server_builder& port(unsigned short v) { port_ = v; return *this; }
    server_builder& threads(int v) { threads_ = v; return *this; }
    server_builder& apps_dir(std::string_view v) { apps_dir_ = std::string(v); return *this; }
    server_builder& request_timeout(std::chrono::seconds v) { timeout_ = v; return *this; }

    // Extra in-process tables (e.g. a built-in "core" app). Registered
    // without any DLL.
    server_builder& builtin_table(const fut::app_table* t) { builtins_.push_back(t); return *this; }

    // Loads tables, serves until SIGINT/SIGTERM, returns a process exit code.
    int run();

private:
    std::filesystem::path cfg_file_{"config.json"};
    std::optional<std::string> host_;
    std::optional<unsigned short> port_;
    std::optional<int> threads_;
    std::optional<std::string> apps_dir_;
    std::optional<std::chrono::seconds> timeout_;
    std::vector<const fut::app_table*> builtins_;
};

} // namespace fut::exec
