#include "stream.hpp"

#include <boost/asio/steady_timer.hpp>
#include <boost/asio/use_awaitable.hpp>

#include <chrono>
#include <string>

namespace demo {

asio::awaitable<void> stream_controller::events(fut::response& out) {
    out.set_type("text/event-stream").set_header("Cache-Control", "no-cache");
    co_await out.begin();

    for (int i = 0; i < 5; ++i) {
        co_await out.sse("tick", std::to_string(i));
        // The handler keeps its own pacing; the injected loop is the server's.
        if (g_io != nullptr) {
            asio::steady_timer timer{*g_io};
            timer.expires_after(std::chrono::milliseconds{20});
            co_await timer.async_wait(asio::use_awaitable);
        }
    }
}

asio::awaitable<void> stream_controller::chunks(fut::response& out) {
    out.set_type("text/plain; charset=utf-8");
    co_await out.begin();
    for (int i = 0; i < 3; ++i) co_await out.write_line("chunk-" + std::to_string(i));
}

} // namespace demo
