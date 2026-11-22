#pragma once

// Streaming responses -- through the response object itself.
//
// The connection creates a fut::response at the start of the request and binds
// it to the socket. An action that takes fut::response& writes into that object:
//
//     begin()      send the head (status, headers, media type)
//     write()      send one body chunk
//     sse()        send one Server-Sent Event frame
//     end()        finish (the connection loop calls it when the action returns)
//
// An action that never writes leaves the response untouched, and the ordinary
// "return a value" path takes over.

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
