#include "listener.hpp"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <string>
#include <system_error>
#include <thread>
#include <vector>

#include <boost/asio.hpp>

#include <fut/ctx.hpp>
#include <fut/http/route.hpp>
#include <fut/json.hpp>
#include <fut/log.hpp>

#include "app_loader.hpp"
#include "config.hpp"
#include "router.hpp"
#include "server.hpp"

#if defined(_WIN32)
#  include <windows.h>
#else
#  include <unistd.h>
#endif

namespace asio = boost::asio;

namespace fut::exec {
namespace {

// Directory of the running executable (for resolving a relative apps_dir).
std::filesystem::path exe_dir() {
#if defined(_WIN32)
    char buf[1024] = {};
    const DWORD n = GetModuleFileNameA(nullptr, buf, sizeof(buf));
    if (n > 0 && n < sizeof(buf))
        return std::filesystem::path{std::string(buf, n)}.parent_path();
#else
    char buf[4096] = {};
    const ssize_t n = readlink("/proc/self/exe", buf, sizeof(buf));
    if (n > 0) return std::filesystem::path{std::string(buf, static_cast<size_t>(n))}.parent_path();
#endif
    return std::filesystem::current_path();
}

// Read by the built-in /api/core/status route once the router is populated.
const router* g_status_router = nullptr;

struct app_info_dto {
    std::string name{};
    std::string prefix{};
    std::size_t routes{};
};

struct status_dto {
    std::string service{"fut"};
    std::string version{fut::library_version};
    std::uint32_t abi{fut::abi_version};
    std::vector<app_info_dto> apps{};
};

// Built-in introspection route: GET /api/core/status
asio::awaitable<fut::response> core_status(const fut::request&, const fut::path_match&) {
    status_dto dto;
    if (g_status_router)
        for (const auto& a : g_status_router->apps())
            dto.apps.push_back(app_info_dto{a.name, a.prefix, a.routes});
    co_return fut::json(dto);
}

void log_exception(std::exception_ptr ep) {
    if (!ep) return;
    try {
        std::rethrow_exception(std::move(ep));
    } catch (const std::exception& e) {
        fut::log::core_log.error("listener exception: {}", e.what());
    }
}

// Starts the chains; a chain that cannot start (unwritable directory, bad
// pattern) is reported and skipped -- logging must never take the server down.
void install_chains(const std::vector<fut::log::chain_options>& chains) {
    for (const fut::log::chain_options& chain : chains) {
        try {
            fut::log::add_chain(chain);
        } catch (const std::exception& e) {
            std::fprintf(stderr, "fut: cannot start a log chain: %s\n", e.what());
        }
    }
}

} // namespace

std::vector<fut::log::chain_options> server::default_log_chains(
    std::string_view console_pattern, std::string_view file_pattern,
    std::filesystem::path file_path, std::uint64_t max_bytes, std::uint32_t max_files,
    fut::log::level console_min, fut::log::level file_min) {
    return {
        fut::log::chain_options{
            .minimum_level = console_min,
            .formatter = fut::log::pattern_formatter{std::string(console_pattern)},
            .sink = fut::log::console_sink{.color = true},
        },
        fut::log::chain_options{
            .minimum_level = file_min,
            .formatter = fut::log::pattern_formatter{std::string(file_pattern)},
            .sink = fut::log::file_sink{.path = std::move(file_path),
                                        .max_bytes = max_bytes,
                                        .max_files = max_files},
        },
    };
}

server& server::log_to_console(bool color, fut::log::level minimum,
                                               std::string_view pattern) {
    chains_.clear();
    chains_explicit_ = true;
    return add_log_chain(fut::log::chain_options{
        .minimum_level = minimum,
        .formatter = fut::log::pattern_formatter{std::string(pattern)},
        .sink = fut::log::console_sink{.color = color},
    });
}

server& server::log_to_file(std::filesystem::path path,
                                            std::uint64_t max_bytes,
                                            std::uint32_t max_files,
                                            fut::log::level minimum,
                                            std::string_view pattern) {
    chains_.clear();
    chains_explicit_ = true;
    return add_log_chain(fut::log::chain_options{
        .minimum_level = minimum,
        .formatter = fut::log::pattern_formatter{std::string(pattern)},
        .sink = fut::log::file_sink{.path = std::move(path),
                                    .max_bytes = max_bytes,
                                    .max_files = max_files},
    });
}

int server::run() {
    int exit_code = 1;
    bool chains_ready = false;
    try {
        config cfg = load_config(cfg_file_);
        if (host_) cfg.host = *host_;
        if (port_) cfg.port = *port_;
        if (threads_) cfg.threads = *threads_;
        if (apps_dir_) cfg.apps_dir = *apps_dir_;
        {
            const std::string level_name = log_level_ ? *log_level_ : cfg.log_level;
            if (const auto parsed = fut::log::from_string<fut::log::level>(level_name)) {
                fut::log::set_level(*parsed);
            } else {
                fut::log::warning("unknown log level '{}', falling back to info", level_name);
            }
        }
        // Chains: an explicit builder policy wins over config.json, which
        // wins over the compiled-in defaults. Patterns, sinks and levels are
        // all caller data -- the server has none of its own.
        install_chains(chains_explicit_ ? chains_ : build_log_chains(cfg.log));
        chains_ready = true;

        const std::chrono::seconds timeout =
            timeout_ ? *timeout_ : std::chrono::seconds(cfg.request_timeout_seconds);

        // A relative apps_dir is resolved against the executable's directory.
        std::filesystem::path apps_dir{cfg.apps_dir};
        if (apps_dir.is_relative()) apps_dir = exe_dir() / apps_dir;

        // Destruction runs in reverse declaration order. The loader is first
        // so the DLLs are unloaded last: the router holds handlers built from
        // DLL code, and the io_context may still hold completions whose
        // handlers live in a DLL. Both must be gone before unload.
        app_loader loader;
        asio::io_context ioc;
        router rt;

        // The io_context is a service too: an app may want to post work or
        // start a timer on the server's event loop.
        modules_.add("io", typeid(asio::io_context).name(), &ioc);

        // App DLLs register themselves into the router through the context.
        // Anything the embedder published with add_module() travels with it.
        loader.load_dir(apps_dir, rt, modules_);

        // Platform middleware wraps everything, so register it first.
        for (const fut::http::middleware_fn& mw : middlewares_) rt.add_global_middleware(mw);

        // Built-in app: the introspection route plus any embedder routes.
        rt.begin_app("core", "/api/core");
        rt.add_route(fut::method::GET, "/api/core/status", &core_status);
        for (const extra_route& r : extra_routes_) rt.add_route(r.method, r.pattern, r.invoke);
        rt.end_app();

        if (error_handler_) rt.set_error_handler(error_handler_);

        rt.finalize();   // wrap every route with its app's chain
        g_status_router = &rt;

        listener lsn(ioc, cfg.host, cfg.port, rt, timeout);

        asio::signal_set signals(ioc, SIGINT, SIGTERM);
        signals.async_wait([&ioc](auto, auto) { ioc.stop(); });

        std::vector<std::thread> pool;
        const int extra = std::max(0, cfg.threads - 1);
        for (int i = 0; i < extra; ++i) pool.emplace_back([&ioc] { ioc.run(); });

        fut::log::core_log.info("fut {} (ABI {}) starting: {} thread(s), {} app(s); Ctrl+C to stop",
                                 fut::library_version, (unsigned)fut::abi_version,
                                 (int)cfg.threads, (unsigned)rt.apps().size());

        asio::co_spawn(ioc, lsn.listen(), log_exception);

        ioc.run();
        for (auto& t : pool) t.join();

        g_status_router = nullptr;
        fut::log::core_log.info("stopped");
        exit_code = 0;
    } catch (const std::exception& e) {
        if (!chains_ready) {
            install_chains(default_log_chains());
            chains_ready = true;
        }
        fut::log::core_log.critical("fatal: {}", e.what());
    }

    // Flush and join the worker threads before the CRT starts tearing down.
    fut::log::shutdown();
    return exit_code;
}

} // namespace fut::exec
