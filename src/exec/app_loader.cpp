#include "app_loader.hpp"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <stdexcept>
#include <system_error>

#include <fut/version.hpp>

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
using app_create_fn  = fut::app* (*)();
using app_destroy_fn = void (*)(fut::app*);

} // namespace

std::vector<app_loader::entry> app_loader::load_dir(const std::filesystem::path& dir) {
    std::vector<entry> out;
    std::error_code ec;
    if (!std::filesystem::is_directory(dir, ec))
        throw std::runtime_error("apps dir not found: " + dir.string());

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
            std::fprintf(stderr, "[app_loader] cannot load %s, skipped\n", p.string().c_str());
            continue;
        }
        auto ver     = reinterpret_cast<abi_version_fn>(dll_sym(h, "fut_abi_version"));
        auto create  = reinterpret_cast<app_create_fn>(dll_sym(h, "fut_app_create"));
        auto destroy = reinterpret_cast<app_destroy_fn>(dll_sym(h, "fut_app_destroy"));
        if (!ver || !create || !destroy) {
            dll_close(h); // 不是 fut app 库
            continue;
        }
        const std::uint32_t dll_abi = (*ver)();
        if (dll_abi != fut::abi_version) {
            std::fprintf(stderr, "[app_loader] %s ABI mismatch (dll=%u, exec=%u), skipped\n",
                         p.string().c_str(), (unsigned)dll_abi, (unsigned)fut::abi_version);
            dll_close(h);
            continue;
        }
        fut::app* instance = (*create)();
        if (!instance) {
            dll_close(h);
            continue;
        }
        std::fprintf(stdout, "[app_loader] app '%s' (prefix '%s') <- %s\n",
                     std::string(instance->name()).c_str(),
                     std::string(instance->prefix()).c_str(),
                     p.string().c_str());
        const entry e{p, instance, h, destroy};
        out.push_back(e);
        loaded_.push_back(e);
    }
    return out;
}

void app_loader::unload_all() {
    for (const auto& e : loaded_) {
        if (e.instance && e.destroy) (*e.destroy)(e.instance);
        if (e.handle) dll_close(e.handle);
    }
    loaded_.clear();
}

app_loader::~app_loader() { unload_all(); }

} // namespace fut::exec
