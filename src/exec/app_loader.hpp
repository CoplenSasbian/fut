#pragma once

// Loads app DLLs and resolves the exported fut::app_table for each.
// Every DLL in the directory is probed; non-fut DLLs and ABI mismatches are
// skipped with a warning.

#include <filesystem>
#include <vector>

#include <fut/route.hpp>

namespace fut::exec {

class app_loader {
public:
    struct entry {
        std::filesystem::path dll{};
        const fut::app_table* table = nullptr;
        void* handle = nullptr;
    };

    // Throws std::runtime_error if the directory does not exist.
    std::vector<entry> load_dir(const std::filesystem::path& dir);

    // Destroys the tables' host module handles (tables become invalid).
    void unload_all();

    ~app_loader();
    app_loader() = default;
    app_loader(const app_loader&) = delete;
    app_loader& operator=(const app_loader&) = delete;

private:
    std::vector<void*> handles_;
};

} // namespace fut::exec
