#pragma once

// TCP listener: spawns one session coroutine per connection, parses HTTP/1.1
// with llhttp and dispatches requests against the router. This is the piece
// fut::exec::server owns; embedders talk to the server, not to the listener.

#include <chrono>
#include <string>

#include <boost/asio.hpp>

#include "router.hpp"

namespace fut::exec {

class listener {
public:
    listener(boost::asio::io_context& ioc, std::string host, unsigned short port,
           router& rt, std::chrono::seconds timeout);

    boost::asio::awaitable<void> listen();

private:
    boost::asio::io_context& ioc_;
    std::string host_;
    unsigned short port_;
    boost::asio::ip::tcp::acceptor acceptor_;
    router& router_;
    std::chrono::seconds timeout_;
};

} // namespace fut::exec
