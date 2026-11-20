#include "app_loader.hpp"

#include <algorithm>
#include <cctype>
#include <stdexcept>
#include <system_error>

#include <fut/log.hpp>

#if defined(_WIN32)
#  include <windows.h>
#else
#  include <dlfcn.h>
#endif

namespace fut::exec {
namespace {

void* dll_open(const char* path) {
#if defined(_WIN32)
    return reinterpret_cast<void*>(LoadLibraryA(path));
#else
    return dlopen(path, RTLD_NOW | RTLD_LOCAL);
#endif
}

void* dll_sym(void* h, const char* name) {
#if defined(_WIN32)
    return reinterpret_cast<void*>(GetProcAddress(reinterpret_cast<HMODULE>(h), name));
#else
    return dlsym(h, name);
#endif
}

void dll_close(void* h) {
#if defined(_WIN32)
    FreeLibrary(reinterpret_cast<HMODULE>(h));
#else
    dlclose(h);
#endif
}

using abi_version_fn = std::uint32_t (*)();
using table_fn = const fut::app_table* (*)();

constexpr auto loader_log = fut::log::grp("loader");

} // namespace

std::vector<app_loader::entry> app_loader::load_dir(const std::filesystem::path& dir) {
    std::vector<entry> out;
    std::error_code ec;
    if (!std::filesystem::is_directory(dir, ec))
        throw std::runtime_error("apps directory not found: " + dir.string());

    std::vector<std::filesystem::path> files;
    for (const auto& f : std::filesystem::directory_iterator(dir, ec)) {
        if (!f.is_regular_file(ec)) continue;
        std::string ext = f.path().extension().string();
        std::ranges::transform(ext, ext.begin(),
                               [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        if (ext == ".dll" || ext == ".so") files.push_back(f.path());
    }
    std::ranges::sort(files);

    for (const auto& p : files) {
        void* h = dll_open(p.string().c_str());
        if (!h) {
            loader_log.warning("cannot load {}, skipped", p.string());
            continue;
        }
        auto ver = reinterpret_cast<abi_version_fn>(dll_sym(h, "fut_abi_version"));
        auto get_table = reinterpret_cast<table_fn>(dll_sym(h, "fut_app_table"));
        if (!ver || !get_table) {
            dll_close(h);  // not a fut app DLL
            continue;
        }
        const std::uint32_t dll_abi = (*ver)();
        if (dll_abi != fut::abi_version) {
            loader_log.warning("{} ABI mismatch (dll={}, server={}), skipped", p.string(),
                               (unsigned)dll_abi, (unsigned)fut::abi_version);
            dll_close(h);
            continue;
        }
        const fut::app_table* table = (*get_table)();
        if (!table || table->route_count == 0 || !table->routes) {
            dll_close(h);
            continue;
        }
        loader_log.info("app '{}' (prefix '{}', {} route(s)) <- {}",
                         std::string(table->name), std::string(table->prefix),
                         (unsigned)table->route_count, p.string());
        out.push_back(entry{p, table, h});
        handles_.push_back(h);
    }
    return out;
}

void app_loader::unload_all() {
    for (void* h : handles_)
        if (h) dll_close(h);
    handles_.clear();
}

app_loader::~app_loader() { unload_all(); }

} // namespace fut::exec
