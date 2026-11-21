#pragma once

// Loads app DLLs and lets them register into the router.
//
// A fut app DLL exports:
//   fut_app_register(fut_ctx*)   the one function the server calls (required)
//   fut_abi_version()            ABI check (optional but expected)
//   fut_app_name() / _prefix()   reporting only (optional)
//
// Every DLL in the directory is probed; non-fut DLLs and ABI mismatches are
// skipped with a warning.

#include <cstddef>
#include <filesystem>
#include <string>
#include <vector>

#include "module_registry.hpp"
#include "router.hpp"

namespace fut::exec {

class app_loader {
public:
    struct entry {
        std::filesystem::path dll{};
        std::string name{};
        std::string prefix{};
        std::size_t routes = 0;
        void* handle = nullptr;
    };

    // What an app module can reach through fut_ctx: the router, plus any
    // other service the executable publishes. Add modules with
    // `services.modules.add(name, typeid(T).name(), &instance)` before loading.
    struct services {
        router* rt = nullptr;
        module_registry modules{};
        std::string app_name{};
        std::string app_prefix{};
    };

    // Throws std::runtime_error if the directory does not exist. Routes are
    // registered into `rt` while loading; the DLLs stay loaded until
    // unload_all() (their route functions live in them).
    std::vector<entry> load_dir(const std::filesystem::path& dir, router& rt,
                                module_registry modules = {});

    void unload_all();

    ~app_loader();
    app_loader() = default;
    app_loader(const app_loader&) = delete;
    app_loader& operator=(const app_loader&) = delete;

private:
    std::vector<void*> handles_;
};

} // namespace fut::exec
