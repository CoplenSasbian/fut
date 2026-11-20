#include "builder.hpp"

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

} // namespace

int server_builder::run() {
    // Logging chains: colourised console plus a rotating file. The global level
    // comes from the configuration and is applied after it is read.
    fut::log::add_chain(fut::log::chain_options{
        .minimum_level = fut::log::level::info,
        .formatter = fut::log::pattern_formatter{"{T} [{L:<8}] {g:<12} {m}"},
        .sink = fut::log::console_sink{.color = true},
    });
    fut::log::add_chain(fut::log::chain_options{
        .minimum_level = fut::log::level::trace,
        .formatter = fut::log::pattern_formatter{"{T} [{L:<8}] {g:<12} {s}: {m}"},
        .sink = fut::log::file_sink{.path = "logs/fut.log",
                                    .max_bytes = 2 * 1024 * 1024, .max_files = 3},
    });

    int exit_code = 1;
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
        const std::chrono::seconds timeout =
            timeout_ ? *timeout_ : std::chrono::seconds(cfg.request_timeout_seconds);

        // A relative apps_dir is resolved against the executable's directory.
        std::filesystem::path apps_dir{cfg.apps_dir};
        if (apps_dir.is_relative()) apps_dir = exe_dir() / apps_dir;

        asio::io_context ioc;
        router rt;

        // App DLLs register themselves into the router through the context.
        app_loader loader;
        loader.load_dir(apps_dir, rt);

        // Built-in app: the introspection route plus any embedder routes.
        rt.begin_app("core", "/api/core");
        rt.add_route(fut::method::GET, "/api/core/status", &core_status);
        for (const extra_route& r : extra_routes_) rt.add_route(r.method, r.pattern, r.invoke);
        rt.end_app();
        g_status_router = &rt;

        server srv(ioc, cfg.host, cfg.port, rt, timeout);

        asio::signal_set signals(ioc, SIGINT, SIGTERM);
        signals.async_wait([&ioc](auto, auto) { ioc.stop(); });

        std::vector<std::thread> pool;
        const int extra = std::max(0, cfg.threads - 1);
        for (int i = 0; i < extra; ++i) pool.emplace_back([&ioc] { ioc.run(); });

        fut::log::core_log.info("fut {} (ABI {}) starting: {} thread(s), {} app(s); Ctrl+C to stop",
                                 fut::library_version, (unsigned)fut::abi_version,
                                 (int)cfg.threads, (unsigned)rt.apps().size());

        asio::co_spawn(ioc, srv.listen(), log_exception);

        ioc.run();
        for (auto& t : pool) t.join();

        g_status_router = nullptr;
        fut::log::core_log.info("stopped");
        exit_code = 0;
    } catch (const std::exception& e) {
        fut::log::core_log.critical("fatal: {}", e.what());
    }

    // Flush and join the worker threads before the CRT starts tearing down.
    fut::log::shutdown();
    return exit_code;
}

} // namespace fut::exec
