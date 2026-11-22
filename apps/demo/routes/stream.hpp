#pragma once

// Streaming: the same response object, written while the action runs.

#include "../common.hpp"
#include "../services.hpp"

namespace demo {

struct [[=fut::controller{}]] stream_controller {

    // text/event-stream: five ticks, 20 ms apart.
    [[=fut::http_get("/events")]]
    asio::awaitable<void> events(fut::response& out);

    // A plain chunked body, one line per chunk.
    [[=fut::http_get("/stream")]]
    asio::awaitable<void> chunks(fut::response& out);
};

} // namespace demo
