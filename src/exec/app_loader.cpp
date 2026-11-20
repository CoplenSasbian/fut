#include "app_loader.hpp"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <stdexcept>
#include <string_view>
#include <system_error>
#include <utility>

#include <fut/ctx.hpp>
#include <fut/export.hpp>
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

using abi_fn = std::uint32_t (*)();
using text_fn = const char* (*)();
using register_fn = void (*)(fut::fut_ctx*);

// The only module the server publishes today.
void* module_lookup(fut::fut_ctx* ctx, std::string_view name) {
    if (name == "router") return const_cast<fut::http::router_module*>(static_cast<router*>(ctx->host)->module());
    return nullptr;
}

constexpr auto loader_log = fut::log::grp("loader");

} // namespace

std::vector<app_loader::entry> app_loader::load_dir(const std::filesystem::path& dir,
                                                    router& rt) {
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
        auto reg = reinterpret_cast<register_fn>(dll_sym(h, "fut_app_register"));
        if (!reg) {  // not a fut app DLL
            dll_close(h);
            continue;
        }
        if (auto ver = reinterpret_cast<abi_fn>(dll_sym(h, "fut_abi_version"))) {
            const std::uint32_t dll_abi = (*ver)();
            if (dll_abi != fut::abi_version) {
                loader_log.warning("{} ABI mismatch (dll={}, server={}), skipped", p.string(),
                                   (unsigned)dll_abi, (unsigned)fut::abi_version);
                dll_close(h);
                continue;
            }
        }
        const std::string name = [&] {
            auto f = reinterpret_cast<text_fn>(dll_sym(h, "fut_app_name"));
            return f ? std::string((*f)()) : p.stem().string();
        }();
        const std::string prefix = [&] {
            auto f = reinterpret_cast<text_fn>(dll_sym(h, "fut_app_prefix"));
            return f ? std::string((*f)()) : std::string{};
        }();

        rt.begin_app(name, prefix);
        fut::fut_ctx ctx{};
        ctx.host = &rt;
        ctx.get_module_fn = &module_lookup;
        (*reg)(&ctx);
        const std::size_t routes = rt.apps().back().routes;
        rt.end_app();

        if (routes == 0) {
            loader_log.warning("app '{}' registered no routes, skipped", name);
            dll_close(h);
            continue;
        }
        loader_log.info("app '{}' (prefix '{}', {} route(s)) <- {}", name, prefix,
                        (unsigned)routes, p.string());
        out.push_back(entry{p, name, prefix, routes, h});
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
