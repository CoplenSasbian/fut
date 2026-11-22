#pragma once

// Streaming responses: a chunked body, and Server-Sent Events.
//
// A streaming action takes fut::stream& (and returns awaitable<void>): it sends
// the head itself, then writes body pieces while the client is already reading.
// The transport is HTTP/1.1 chunked, so no Content-Length is needed and the
// connection stays keep-alive afterwards.
//
// Every write() awaits the socket: that is the backpressure. A client that
// reads slowly slows the producer down instead of growing a buffer.

#include "../common.hpp"

namespace demo {

struct [[=fut::controller{}]] stream_controller {

    // text/event-stream: five ticks, 20 ms apart.
    [[=fut::http_get("/events")]]
    asio::awaitable<void> events(fut::stream& out);

    // A plain chunked body, one line per chunk.
    [[=fut::http_get("/stream")]]
    asio::awaitable<void> chunks(fut::stream& out);
};

} // namespace demo
