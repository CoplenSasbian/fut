#include "stream.hpp"

#include <chrono>
#include <string>

namespace demo {

asio::awaitable<void> stream_controller::events(fut::stream& out) {
    fut::response head;
    head.content_type = "text/event-stream";
    head.headers.emplace_back("Cache-Control", "no-cache");
    co_await out.begin(std::move(head));

    for (int i = 0; i < 5; ++i) {
        co_await out.send("tick", std::to_string(i));      // event: tick / data: i
        co_await out.sleep(std::chrono::milliseconds{20}); // on the server's loop
    }
}

asio::awaitable<void> stream_controller::chunks(fut::stream& out) {
    co_await out.begin(fut::response::text(""));
    for (int i = 0; i < 3; ++i) co_await out.line("chunk-" + std::to_string(i));
}

} // namespace demo
