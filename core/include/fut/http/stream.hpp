#pragma once

// fut/http/stream.hpp -- streaming responses: chunked bodies and SSE.
//
// An action normally returns a response, and the server serializes it whole.
// Streaming flips that: the action gets a fut::stream, sends the head (status
// and headers) and then writes body pieces as it produces them, while the
// client is already reading.
//
//     [[=fut::http_get("/events")]]
//     asio::awaitable<void> events(fut::stream& out) {
//         co_await out.begin({.content_type = "text/event-stream"});
//         for (int i = 0; i < 10; ++i) {
//             co_await out.send("tick", std::to_string(i));   // SSE framing
//             co_await out.sleep(std::chrono::seconds{1});
//         }
//     }
//
// Transport: HTTP/1.1 chunked transfer-encoding, so no Content-Length is
// needed and the connection can stay keep-alive afterwards. write() awaits the
// socket, which is the backpressure: a slow client slows the producer down
// instead of growing an unbounded buffer.
//
// A handler may take fut::stream& and still return a normal response: if it
// never calls begin(), the server sends the returned response as usual.

#include <chrono>
#include <cstddef>
#include <functional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <boost/asio.hpp>

#include <fut/http/route.hpp>

namespace fut {

// Thrown by stream operations when the client went away (or the stream is not
// attached, e.g. when a test drives the router directly). Let it propagate: the
// connection loop treats it as "stop producing".
class stream_error : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

inline constexpr std::size_t stream_flush_chunk = 16 * 1024;

// The producer's handle on the connection.
class stream {
public:
    // What the server binds behind the handle. Both callbacks run on the
    // connection's executor.
    using open_fn = std::function<boost::asio::awaitable<void>(
        unsigned status, std::string_view content_type,
        const std::vector<std::pair<std::string, std::string>>& headers)>;
    using write_fn = std::function<boost::asio::awaitable<void>(std::string_view bytes)>;
    using finish_fn = std::function<boost::asio::awaitable<void>()>;
    using timer_fn = std::function<boost::asio::awaitable<void>(std::chrono::milliseconds)>;

    stream() = default;

    stream(open_fn open, write_fn write, finish_fn finish, timer_fn sleep)
        : open_(std::move(open)),
          write_(std::move(write)),
          finish_(std::move(finish)),
          sleep_(std::move(sleep)) {}

    [[nodiscard]] bool started() const noexcept { return started_; }
    [[nodiscard]] bool closed() const noexcept { return closed_; }

    // Sends the status line and the headers; the body is chunked from here on.
    // Only the first call has an effect; later calls just update nothing.
    boost::asio::awaitable<void> begin(response head) {
        if (started_) co_return;
        if (!open_) {
            closed_ = true;
            throw stream_error("stream is not attached to a connection");
        }
        started_ = true;
        co_await open_(head.status, head.content_type, head.headers);
    }

    // One body chunk (framed by the server). Awaiting it is the backpressure.
    boost::asio::awaitable<void> write(std::string_view bytes) {
        if (closed_) throw stream_error("stream closed");
        if (!write_) {
            closed_ = true;
            throw stream_error("stream is not attached to a connection");
        }
        co_await write_(bytes);
    }

    // One body chunk that ends with a newline -- what text streams want.
    boost::asio::awaitable<void> line(std::string_view text) {
        std::string buf{text};
        buf.push_back('\n');
        co_await write(buf);
    }

    // text/event-stream: "event:" + "data:" + blank line.
    boost::asio::awaitable<void> send(std::string_view event, std::string_view data) {
        std::string frame;
        frame.reserve(event.size() + data.size() + 16);
        if (!event.empty()) {
            frame.append("event: ");
            frame.append(event);
            frame.push_back('\n');
        }
        // SSE data may not contain raw newlines; prefix every line.
        std::size_t begin = 0;
        while (begin <= data.size()) {
            const std::size_t end = data.find('\n', begin);
            frame.append("data: ");
            frame.append(data.substr(begin, end == std::string_view::npos
                                                ? std::string_view::npos
                                                : end - begin));
            frame.push_back('\n');
            if (end == std::string_view::npos) break;
            begin = end + 1;
        }
        frame.push_back('\n');
        co_await write(frame);
    }

    // SSE comment: the standard keep-alive on a quiet stream.
    boost::asio::awaitable<void> comment(std::string_view text) {
        std::string frame{": "};
        frame.append(text);
        frame.append("\n\n");
        co_await write(frame);
    }

    // Waits on the server's io_context. The action's own timer would work too;
    // this one keeps a streaming handler from needing an injected service.
    boost::asio::awaitable<void> sleep(std::chrono::milliseconds delay) {
        if (!sleep_) {
            closed_ = true;
            throw stream_error("stream is not attached to a connection");
        }
        co_await sleep_(delay);
    }

    // Ends the body ("0\r\n\r\n"). The connection loop calls it when the action
    // returns; calling it explicitly just makes the moment visible.
    boost::asio::awaitable<void> finish() {
        if (!started_ || closed_) co_return;
        if (finish_) co_await finish_();
        closed_ = true;
    }

    [[nodiscard]] bool attached() const noexcept { return static_cast<bool>(write_); }

private:
    open_fn open_{};
    write_fn write_{};
    finish_fn finish_{};
    timer_fn sleep_{};
    bool started_ = false;
    bool closed_ = false;
};

} // namespace fut
