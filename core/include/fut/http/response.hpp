#pragma once

// fut/http/response.hpp -- the response: what to send, and the writing of it.
//
// One type, two modes:
//
//   * unbound (no connection): a plain value. An action fills status, headers,
//     media type and body and returns it; the server copies it into the
//     connection's response and that response writes it.
//   * bound (created by the connection at the start of the request): the same
//     object, able to write itself -- begin() sends the head, write() sends a
//     body chunk, end() finishes. This is what makes streaming possible without
//     a second, parallel abstraction.
//
//     // ordinary
//     [[=fut::http_post("/items")]]
//     asio::awaitable<item> create([[=fut::from_body{}]] item in) { co_return in; }
//
//     // streaming: the connection's response is handed to the action
//     [[=fut::http_get("/events")]]
//     asio::awaitable<void> events(fut::response& out) {
//         out.set_type("text/event-stream");
//         for (int i = 0; i < 5; ++i) co_await out.sse("tick", std::to_string(i));
//     }
//
// Before the first write the head is not sent yet, so status, headers and media
// type can still be changed; after it they are on the wire.

#include <cstddef>
#include <functional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <boost/asio/awaitable.hpp>

namespace fut {

// Thrown when a response cannot write: not bound to a connection, or the client
// is gone. Let it propagate; the connection loop stops producing.
class stream_error : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

class response {
public:
    // The connection's side: how a bound response reaches the socket. Function
    // pointers, deliberately -- the response travels to a DLL and back.
    struct sink {
        std::function<boost::asio::awaitable<void>(const response& head)> begin{};
        std::function<boost::asio::awaitable<void>(std::string_view bytes)> chunk{};
        std::function<boost::asio::awaitable<void>()> finish{};
        std::function<boost::asio::awaitable<void>(const response& r)> whole{};
    };

    // ---- what to send ----
    unsigned status = 200;
    std::string content_type = "application/json";
    std::string body{};
    std::vector<std::pair<std::string, std::string>> headers{};

    response() = default;
    response(unsigned s, std::string ct, std::string b)
        : status(s), content_type(std::move(ct)), body(std::move(b)) {}

    // ---- factories (unbound values) ----
    static response json(std::string payload, unsigned code = 200) {
        return response{code, "application/json", std::move(payload)};
    }
    static response text(std::string payload, unsigned code = 200) {
        return response{code, "text/plain; charset=utf-8", std::move(payload)};
    }
    static response html(std::string payload, unsigned code = 200) {
        return response{code, "text/html; charset=utf-8", std::move(payload)};
    }
    static response no_content() { return response{204, {}, {}}; }
    static response redirect(std::string location, unsigned code = 302) {
        response r{code, "text/plain; charset=utf-8", "redirecting to " + location};
        r.headers.emplace_back("Location", std::move(location));
        return r;
    }

    // ---- fluent editing ----
    response& set_status(unsigned code) noexcept {
        status = code;
        return *this;
    }
    response& set_body(std::string payload) {
        body = std::move(payload);
        return *this;
    }
    response& set_type(std::string type) {
        content_type = std::move(type);
        return *this;
    }
    response& set_header(std::string name, std::string value) {
        for (auto& [key, existing] : headers)
            if (iequals(key, name)) {
                existing = std::move(value);
                return *this;
            }
        headers.emplace_back(std::move(name), std::move(value));
        return *this;
    }
    response& add_header(std::string name, std::string value) {
        headers.emplace_back(std::move(name), std::move(value));
        return *this;
    }
    response& set_cookie(std::string name, std::string value,
                         std::string_view attributes = "Path=/; HttpOnly") {
        std::string cookie = std::move(name) + "=" + std::move(value);
        if (!attributes.empty()) {
            cookie.append("; ");
            cookie.append(attributes);
        }
        return add_header("Set-Cookie", std::move(cookie));
    }
    response& erase_header(std::string_view name) {
        for (auto it = headers.begin(); it != headers.end(); ++it)
            if (iequals(it->first, name)) {
                headers.erase(it);
                break;
            }
        return *this;
    }

    [[nodiscard]] std::string_view header(std::string_view name) const {
        for (const auto& [key, value] : headers)
            if (iequals(key, name)) return value;
        return {};
    }
    [[nodiscard]] bool has_header(std::string_view name) const { return !header(name).empty(); }
    [[nodiscard]] bool ok() const noexcept { return status >= 200 && status < 300; }

    // ---- binding: the connection owns this part ----
    void bind(const sink* connection_sink) noexcept { sink_ = connection_sink; }
    [[nodiscard]] bool bound() const noexcept { return sink_ != nullptr; }
    [[nodiscard]] bool started() const noexcept { return started_; }
    [[nodiscard]] bool finished() const noexcept { return finished_; }

    // Back to the state of a fresh request (the connection reuses it).
    void reset() {
        status = 200;
        content_type = "application/json";
        body.clear();
        headers.clear();
        started_ = false;
        finished_ = false;
    }

    // Takes another response's content (the value an action returned).
    response& absorb(const response& other) {
        status = other.status;
        content_type = other.content_type;
        body = other.body;
        headers = other.headers;
        return *this;
    }

    // ---- writing (bound responses only) ----
    boost::asio::awaitable<void> begin() {
        if (started_ || finished_) co_return;
        if (sink_ == nullptr || !sink_->begin)
            throw stream_error("response is not bound to a connection");
        started_ = true;
        co_await sink_->begin(*this);
    }

    // A body chunk. The first write sends the head, so the body becomes chunked.
    boost::asio::awaitable<void> write(std::string_view bytes) {
        if (finished_) throw stream_error("response already finished");
        co_await begin();
        if (sink_ == nullptr || !sink_->chunk)
            throw stream_error("response is not bound to a connection");
        co_await sink_->chunk(bytes);
    }

    boost::asio::awaitable<void> write_line(std::string_view text) {
        std::string buf{text};
        buf.push_back('\n');
        co_await write(buf);
    }

    // Server-Sent Events: "event:" + "data:" + blank line.
    boost::asio::awaitable<void> sse(std::string_view event, std::string_view data) {
        std::string frame;
        frame.reserve(event.size() + data.size() + 16);
        if (!event.empty()) {
            frame.append("event: ");
            frame.append(event);
            frame.push_back('\n');
        }
        std::size_t pos = 0;
        for (;;) {
            const std::size_t eol = data.find('\n', pos);
            frame.append("data: ");
            frame.append(eol == std::string_view::npos ? data.substr(pos)
                                                       : data.substr(pos, eol - pos));
            frame.push_back('\n');
            if (eol == std::string_view::npos) break;
            pos = eol + 1;
        }
        frame.push_back('\n');
        co_await write(frame);
    }

    // An SSE comment: the keep-alive for a quiet stream.
    boost::asio::awaitable<void> sse_comment(std::string_view text) {
        std::string frame{": "};
        frame.append(text);
        frame.append("\n\n");
        co_await write(frame);
    }

    // Finishes the message: the last chunk when streaming, or the whole head +
    // body when the action never wrote (the normal path).
    boost::asio::awaitable<void> end() {
        if (finished_) co_return;
        finished_ = true;
        if (sink_ == nullptr) co_return;   // a value: the caller sends it
        if (started_) {
            if (sink_->finish) co_await sink_->finish();
            co_return;
        }
        if (sink_->whole) co_await sink_->whole(*this);
    }

private:
    static bool iequals(std::string_view a, std::string_view b) {
        if (a.size() != b.size()) return false;
        for (std::size_t i = 0; i < a.size(); ++i) {
            const auto ca = static_cast<unsigned char>(a[i]);
            const auto cb = static_cast<unsigned char>(b[i]);
            if (std::tolower(ca) != std::tolower(cb)) return false;
        }
        return true;
    }

    const sink* sink_ = nullptr;
    bool started_ = false;
    bool finished_ = false;
};

} // namespace fut
