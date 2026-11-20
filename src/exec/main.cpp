// fut_server 主程序：加载 bin/apps 下的业务 DLL，前缀路由，Beast/Asio 协程 HTTP 服务。

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <memory>
#include <string>
#include <system_error>
#include <thread>
#include <vector>

#include <boost/asio.hpp>

#include <fut/app.hpp>
#include <fut/json.hpp>
#include <fut/version.hpp>

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

namespace {

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

// 内置 core app（进程内实现同一接口）：GET /api/core/status
class core_app final : public fut::app {
public:
    explicit core_app(const fut::exec::router& rt) : router_(rt) {}

    std::string_view name() const override { return "core"; }
    std::string_view prefix() const override { return "/api/core"; }

    asio::awaitable<fut::response> handle(const fut::request& req) override {
        if (req.method == "GET" && req.path == "/status") {
            struct app_info { std::string name; std::string prefix; };
            struct status_dto {
                std::string service = "fut";
                std::string version = fut::server_version;
                std::uint32_t abi = fut::abi_version;
                std::vector<app_info> apps;
            };
            status_dto dto;
            for (const fut::app* a : router_.apps())
                dto.apps.push_back({std::string(a->name()), std::string(a->prefix())});
            co_return fut::json(dto);
        }
        co_return fut::error(404, "core: no such route");
    }

private:
    const fut::exec::router& router_;
};

} // namespace

int main(int argc, char** argv) {
    std::filesystem::path cfg_path = "config.json";
    std::filesystem::path apps_dir_override;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "-c" && i + 1 < argc) cfg_path = argv[++i];
        else if (a == "--apps-dir" && i + 1 < argc) apps_dir_override = argv[++i];
        else {
            std::fprintf(stderr, "usage: fut_server [-c config.json] [--apps-dir <dir>]\n");
            return 0;
        }
    }

    try {
        fut::exec::config cfg = fut::exec::load_config(cfg_path);
        if (!apps_dir_override.empty()) cfg.apps_dir = apps_dir_override.string();

        // apps_dir 为相对路径时，相对 exe 所在目录解析（默认 bin/apps）
        std::filesystem::path apps_dir{cfg.apps_dir};
        if (apps_dir.is_relative()) apps_dir = exe_dir() / apps_dir;

        asio::io_context ioc;
        fut::exec::router rt;
        fut::exec::app_loader loader;
        for (const auto& e : loader.load_dir(apps_dir)) rt.add(e.instance);

        auto core = std::make_unique<core_app>(rt);
        rt.add(core.get());

        fut::exec::server srv(ioc, cfg.host, cfg.port, rt,
                              std::chrono::seconds(cfg.request_timeout_seconds));

        asio::signal_set signals(ioc, SIGINT, SIGTERM);
        signals.async_wait([&ioc](auto, auto) { ioc.stop(); });

        std::vector<std::thread> pool;
        const int extra = std::max(0, cfg.threads - 1);
        for (int i = 0; i < extra; ++i) pool.emplace_back([&ioc] { ioc.run(); });

        std::fprintf(stdout,
                     "[main] fut %s (ABI %u) starting: %d threads, %d business app(s); Ctrl+C to stop\n",
                     fut::server_version, (unsigned)fut::abi_version,
                     (int)cfg.threads, (int)(rt.apps().size() - 1));

        asio::co_spawn(ioc, srv.listen(), [](std::exception_ptr ep) {
            if (ep) {
                try { std::rethrow_exception(std::move(ep)); }
                catch (const std::exception& e) { std::fprintf(stderr, "[listen] exception: %s\n", e.what()); }
            }
        });

        ioc.run();
        for (auto& t : pool) t.join();

        core.reset();
        std::fprintf(stdout, "[main] stopped\n");
    } catch (const std::exception& e) {
        std::fprintf(stderr, "[main] fatal: %s\n", e.what());
        return 1;
    }
    return 0;
}
