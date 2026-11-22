#include "listener.hpp"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <csignal>
#include <cstdio>
#include <filesystem>
#include <functional>
#include <string>
#include <system_error>
#include <thread>
#include <vector>

#include <boost/asio.hpp>

#include <fut/ctx.hpp>
#include <fut/http/route.hpp>
#include <fut/json.hpp>
#include <fut/log.hpp>
#include <fut/platform.hpp>

#include <fut/exec/app_loader.hpp>
#include <fut/exec/config.hpp>
#include <fut/exec/router.hpp>
#include <fut/exec/server.hpp>

namespace asio = boost::asio;

namespace fut::exec {
namespace {
// What an app's group_ref calls. It goes straight into this module's log core,
// so app lines share the chains, level filter and formatting with ours.
void app_log_write(void*, fut::log::level severity, std::string_view group,
                   std::string_view message, std::string_view file,
                   std::string_view function, std::uint32_t line,
                   std::uint32_t column) {
    if (static_cast<std::int32_t>(severity) < static_cast<std::int32_t>(fut::log::current_level()))
        return;
    fut::log::submit(
        severity, group, message, file, function, line, column,
        std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::system_clock::now().time_since_epoch())
            .count(),
        static_cast<std::uint64_t>(std::hash<std::thread::id>{}(std::this_thread::get_id())));
}

const fut::log::service app_log_service{nullptr, &app_log_write};

// Directory of the running executable (for resolving a relative apps_dir).
// The platform layer knows how to ask each system.
std::filesystem::path exe_dir() {
    std::error_code ec;
    const auto exe = fut::platform::executable_path();
    if (exe.empty()) return std::filesystem::current_path(ec);
    return exe.parent_path();
}

// Read by the built-in /api/core/status route once the router is populated.
const router* g_status_router = nullptr;
// The generation currently live, and the host that can install a new one.
std::size_t g_generation = 0;
const std::size_t* g_status_generation = nullptr;
app_host* g_app_host = nullptr;

struct app_info_dto {
    std::string name{};
    std::string prefix{};
    std::size_t routes{};
};

struct status_dto {
    std::string service{"fut"};
    std::string version{fut::library_version};
    std::uint32_t abi{fut::abi_version};
    std::size_t generation{};
    std::vector<app_info_dto> apps{};
};

struct reload_dto {
    bool reloaded{};
    std::size_t generation{};
    std::vector<app_info_dto> apps{};
    std::string error{};
};

// Built-in: POST /api/core/reload -- build the next generation and swap it in.
// The request that calls this keeps the previous generation alive until it
// finishes, so the answer is the last thing the old DLLs do.
asio::awaitable<void> core_reload(const fut::request&, const fut::path_match&,
                                  fut::response& out) {
    if (g_app_host == nullptr) {
        out.set_status(503).set_json(R"({"reloaded":false,"error":"server not running"})");
        co_return;
    }

    std::string problem;
    const auto next = g_app_host->reload(problem);

    reload_dto dto;
    dto.reloaded = next != nullptr;
    if (!next) {
        dto.error = std::move(problem);
        out.set_status(500).dto(dto);
        co_return;
    }

    dto.generation = next->id;
    for (const auto& a : next->rt.apps())
        dto.apps.push_back(app_info_dto{a.name, a.prefix, a.routes});

    g_generation = next->id;
    g_status_router = &next->rt;
    out.dto(dto);
    co_return;
}

// Built-in introspection route: GET /api/core/status
asio::awaitable<void> core_status(const fut::request&, const fut::path_match&,
                                 fut::response& out) {
    status_dto dto;
    dto.generation = g_generation;
    if (g_status_router)
        for (const auto& a : g_status_router->apps())
            dto.apps.push_back(app_info_dto{a.name, a.prefix, a.routes});
    out.dto(dto);
    co_return;
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

        asio::io_context ioc;

        // The io_context is a service too: an app may want to post work or
        // start a timer on the server's event loop.
        modules_.add("io", typeid(asio::io_context).name(), &ioc);
        modules_.add("log", typeid(fut::log::service).name(),
                     const_cast<fut::log::service*>(&app_log_service));

        // One builder for every generation: the first load and each reload run
        // exactly the same code, so a reload exercises what start-up does.
        auto build_generation = [&](std::size_t gen_id) -> app_host::generation_ptr {
            auto gen = std::make_shared<app_generation>();
            gen->id = gen_id;

            // App DLLs register themselves into this generation's router.
            gen->apps = gen->loader.load_dir(apps_dir, gen->rt, modules_);

            // Platform middleware wraps everything, so register it first.
            for (const fut::http::middleware_fn& mw : middlewares_)
                gen->rt.add_global_middleware(mw);

            // Built-in app: introspection, reload, plus any embedder routes.
            gen->rt.begin_app("core", "/api/core");
            gen->rt.add_route(fut::method::GET, "/api/core/status", &core_status);
            gen->rt.add_route(fut::method::POST, "/api/core/reload", &core_reload);
            for (const extra_route& r : extra_routes_)
                gen->rt.add_route(r.method, r.pattern, r.invoke);
            gen->rt.end_app();

            if (error_handler_) gen->rt.set_error_handler(error_handler_);
            gen->rt.finalize();   // wrap every route with its app's chain
            return gen;
        };

        app_host host{build_generation};
        std::string problem;
        if (!host.reload(problem))
            throw std::runtime_error("cannot load apps: " + problem);

        const auto first = host.current();
        g_generation = first->id;
        g_status_router = &first->rt;
        g_app_host = &host;

        listener lsn(ioc, cfg.host, cfg.port, host, timeout);

        // SIGINT/SIGTERM stop; SIGHUP reloads (POSIX only -- Windows has no
        // SIGHUP delivery, so there POST /api/core/reload is the trigger).
        asio::signal_set signals(ioc);
        signals.add(SIGINT);
        signals.add(SIGTERM);
#ifdef SIGHUP
        signals.add(SIGHUP);
        constexpr bool sighup_reloads = true;
#else
        constexpr bool sighup_reloads = false;
#endif

        auto on_signal = std::make_shared<std::function<void()>>();
        *on_signal = [&ioc, &host, &signals, on_signal]() {
            signals.async_wait(
                [&ioc, &host, on_signal](const boost::system::error_code& ec, int sig) {
                    if (ec) return;
#ifdef SIGHUP
                    if (sig == SIGHUP) {
                        std::string problem;
                        if (const auto next = host.reload(problem)) {
                            g_generation = next->id;
                            g_status_router = &next->rt;
                            fut::log::core_log.info("reloaded apps: generation {}", next->id);
                        } else {
                            fut::log::core_log.error("reload failed, still serving: {}", problem);
                        }
                        (*on_signal)();   // re-arm: SIGHUP may come again
                        return;
                    }
#else
                    (void)sig;
                    (void)sighup_reloads;
#endif
                    ioc.stop();
                });
        };
        (*on_signal)();

        std::vector<std::thread> pool;
        const int extra = std::max(0, cfg.threads - 1);
        for (int i = 0; i < extra; ++i) pool.emplace_back([&ioc] { ioc.run(); });

        fut::log::core_log.info(
            "fut {} (ABI {}) starting: {} thread(s), {} app(s), generation {}; "
            "Ctrl+C stops, SIGHUP reloads",
            fut::library_version, (unsigned)fut::abi_version, (int)cfg.threads,
            (unsigned)first->apps.size(), (unsigned)first->id);

        asio::co_spawn(ioc, lsn.listen(), log_exception);

        ioc.run();
        for (auto& t : pool) t.join();

        g_app_host = nullptr;
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
