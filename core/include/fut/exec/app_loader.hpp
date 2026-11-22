#pragma once

// Loads app DLLs and lets them register into the router.
//
// A fut app DLL exports:
//   fut_app_register(fut_ctx*)   the one function the server calls (required)
//   fut_abi_version()            ABI check (optional but expected)
//   fut_app_name() / _prefix()   reporting only (optional)
//
// Every shared library in the directory is probed -- .dll on Windows, .so on
// Linux, .dylib on macOS -- and non-fut ones and ABI mismatches are skipped
// with a warning.

#include <cstddef>
#include <filesystem>
#include <string>
#include <vector>

#include <fut/platform.hpp>
#include <fut/exec/module_registry.hpp>
#include <fut/exec/router.hpp>

#include <fut/export.hpp>

namespace fut::exec {

class FUT_API app_loader {
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
    //
    // Lifetime: declare the loader BEFORE the router. Destruction is reverse
    // order, so the router -- which holds handlers and middleware built from
    // the DLLs' code -- must be destroyed first; the loader then unloads last.
    std::vector<entry> load_dir(const std::filesystem::path& dir, router& rt,
                                module_registry modules = {});

    void unload_all();

    ~app_loader();
    app_loader() = default;
    app_loader(const app_loader&) = delete;
    app_loader& operator=(const app_loader&) = delete;

private:
    std::vector<fut::platform::library> libs_;
};

} // namespace fut::exec
