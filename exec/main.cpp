// fut_server entry point -- a thin shell over fut::exec::server, which lives
// in the fut_core library. Nothing here is server machinery.

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <string>

#include <boost/asio.hpp>

#include <fut/http/router.hpp>
#include <fut/log.hpp>

#include <fut/exec/server.hpp>

namespace {

namespace asio = boost::asio;

constexpr auto http_log = fut::log::grp("http");

// Platform middleware: it belongs to the executable, sees every request
// (including 404s) and wraps every app. Compare with an app middleware, which
// an app registers for its own routes only.
asio::awaitable<fut::response> log_request(const fut::request& req,
                                           const fut::path_match& params,
                                           const fut::http::next_fn& next) {
    const auto start = std::chrono::steady_clock::now();
    fut::response res = co_await next(req, params);
    const auto micros = std::chrono::duration_cast<std::chrono::microseconds>(
                            std::chrono::steady_clock::now() - start)
                            .count();
    http_log.info("{} {} -> {} ({} us)", std::string(req.method), std::string(req.target),
                  res.status, micros);
    res.headers.emplace_back("X-Fut-Elapsed-Us", std::to_string(micros));
    co_return res;
}

} // namespace

int main(int argc, char** argv) {
    fut::exec::server srv;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "-c" && i + 1 < argc) srv.config_file(argv[++i]);
        else if (a == "--apps-dir" && i + 1 < argc) srv.apps_dir(argv[++i]);
        else if (a == "--port" && i + 1 < argc)
            srv.port(static_cast<unsigned short>(std::atoi(argv[++i])));
        else if (a == "--threads" && i + 1 < argc) srv.threads(std::atoi(argv[++i]));
        else if (a == "--log-level" && i + 1 < argc) srv.log_level(argv[++i]);
        else {
            std::fputs("usage: fut_server [-c config.json] [--apps-dir DIR]"
                       " [--port N] [--threads N] [--log-level LEVEL]\n", stderr);
            return 0;
        }
    }
    srv.add_middleware(&log_request);
    return srv.run();
}
