#include "stream.hpp"

#include <chrono>
#include <string>

#include <boost/asio/steady_timer.hpp>
#include <boost/asio/use_awaitable.hpp>

namespace demo {

asio::awaitable<void> stream_controller::events(fut::response& out) {
    out.set_type("text/event-stream").set_header("Cache-Control", "no-cache");
    co_await out.begin();

    for (int i = 0; i < 5; ++i) {
        co_await out.sse("tick", std::to_string(i));
        if (g_io != nullptr) {
            asio::steady_timer timer{*g_io};
            timer.expires_after(std::chrono::milliseconds{500});
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
