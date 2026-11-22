#include <fut/exec/app_loader.hpp>

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

#include <fut/platform.hpp>

namespace fut::exec {
namespace {

using abi_fn = std::uint32_t (*)();
using text_fn = const char* (*)();
using register_fn = void (*)(fut::fut_ctx*);

// The executable's module lookup: name and/or type decide, the registry
// answers. This runs on the executable's side of the ABI, so an ambiguous or
// mismatched request is reported as a status and the DLL raises the error.
fut::module_lookup module_lookup(fut::fut_ctx* ctx, std::string_view name,
                                 std::string_view type) {
    auto* services = static_cast<app_loader::services*>(ctx->host);
    if (!services) return {nullptr, fut::module_status::not_found};
    return services->modules.find(name, type);
}

constexpr auto loader_log = fut::log::grp("loader");

} // namespace

std::vector<app_loader::entry> app_loader::load_dir(const std::filesystem::path& dir,
                                                    router& rt,
                                                    module_registry modules) {
    std::vector<entry> out;
    std::error_code ec;
    if (!std::filesystem::is_directory(dir, ec))
        throw std::runtime_error("apps directory not found: " + dir.string());

    std::vector<std::filesystem::path> files;
    for (const auto& f : std::filesystem::directory_iterator(dir, ec)) {
        if (!f.is_regular_file(ec)) continue;
        // .dll / .so / .dylib, case-insensitively; a name that is not a fut app
        // is opened and skipped, so the extension is only a prefilter.
        if (fut::platform::is_library_name(f.path().filename().string()))
            files.push_back(f.path());
    }
    std::ranges::sort(files);

    for (const auto& p : files) {
        std::string why;
        fut::platform::library lib = fut::platform::library::open(p, why);
        if (!lib.valid()) {
            loader_log.warning("cannot load {}: {}; skipped", p.string(), why);
            continue;
        }
        auto reg = reinterpret_cast<register_fn>(lib.symbol("fut_app_register"));
        if (!reg) {  // not a fut app
            continue;
        }
        if (auto ver = reinterpret_cast<abi_fn>(lib.symbol("fut_abi_version"))) {
            const std::uint32_t dll_abi = (*ver)();
            if (dll_abi != fut::abi_version) {
                loader_log.warning("{} ABI mismatch (dll={}, server={}), skipped", p.string(),
                                   (unsigned)dll_abi, (unsigned)fut::abi_version);
                continue;
            }
        }
        const std::string name = [&] {
            auto f = reinterpret_cast<text_fn>(lib.symbol("fut_app_name"));
            return f ? std::string((*f)()) : p.stem().string();
        }();
        const std::string prefix = [&] {
            auto f = reinterpret_cast<text_fn>(lib.symbol("fut_app_prefix"));
            return f ? std::string((*f)()) : std::string{};
        }();

        // The router is always published under its type; "router" is its name.
        if (modules.find("router", typeid(fut::http::router_module).name()).status ==
            fut::module_status::not_found) {
            modules.add("router", typeid(fut::http::router_module).name(),
                        const_cast<fut::http::router_module*>(rt.module()));
        }

        services host_services{&rt, std::move(modules), name, prefix};

        rt.begin_app(name, prefix);
        fut::fut_ctx ctx{};
        ctx.host = &host_services;
        ctx.get_module_fn = &module_lookup;
        ctx.app_name = name;
        ctx.app_prefix = prefix;
        (*reg)(&ctx);
        const std::size_t routes = rt.apps().back().routes;
        rt.end_app();

        if (routes == 0) {
            loader_log.warning("app '{}' registered no routes, skipped", name);
            continue;
        }
        loader_log.info("app '{}' (prefix '{}', {} route(s)) <- {}", name, prefix,
                        (unsigned)routes, p.string());
        out.push_back(entry{p, name, prefix, routes, lib.handle()});
        libs_.push_back(std::move(lib));
    }
    return out;
}

void app_loader::unload_all() {
    libs_.clear();   // each shared_library unloads in its destructor
}

app_loader::~app_loader() { unload_all(); }

} // namespace fut::exec
