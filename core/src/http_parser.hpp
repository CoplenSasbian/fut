#pragma once

// Incremental HTTP/1.1 request parser built on llhttp (the parser Node.js
// uses: small, C, fuzz-tested). It only parses -- the socket loop decides
// when to read and how to reuse the buffer.
//
//   request_parser p;
//   switch (p.feed(buffer.data(), buffer.size())) {
//   case request_parser::status::need_more: buffer.clear(); break;
//   case request_parser::status::complete:  buffer.erase(0, p.consumed()); ...
//   case request_parser::status::error:     ...
//   }

#include <cstddef>
#include <memory>
#include <string_view>

#include "http_message.hpp"

namespace fut::exec {

class request_parser {
public:
    // interim: the header block is complete and the parser stopped before the
    // body (the client asked for 100-continue). message() is filled.
    enum class status { need_more, complete, interim, error };

    request_parser();
    ~request_parser();

    request_parser(const request_parser&) = delete;
    request_parser& operator=(const request_parser&) = delete;

    // Feeds the front of the connection buffer. On `complete` the message is
    // available and consumed() is the number of bytes it used (the rest of
    // the buffer belongs to the next request -- pipelining works).
    status feed(const char* data, std::size_t length);

    [[nodiscard]] const http_request_message& message() const;
    [[nodiscard]] std::size_t consumed() const;
    [[nodiscard]] std::string_view error() const;

    // Continues after status::interim: the body follows in the next feed().
    void resume();

    // Prepares the next message on the same connection.
    void reset();

private:
    struct impl;
    std::unique_ptr<impl> impl_;
    friend struct impl;
};

} // namespace fut::exec
