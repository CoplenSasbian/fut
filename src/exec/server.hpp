#pragma once

#include <chrono>
#include <string>

#include <boost/asio.hpp>
#include <boost/beast.hpp>

#include "router.hpp"

namespace fut::exec {

// TCP 监听器：每个连接 co_spawn 一个会话协程
class server {
public:
    server(boost::asio::io_context& ioc, std::string host, unsigned short port,
           router rt, std::chrono::seconds timeout);

    boost::asio::awaitable<void> listen();

private:
    boost::asio::io_context& ioc_;
    std::string host_;
    unsigned short port_;
    boost::asio::ip::tcp::acceptor acceptor_;
    router router_;
    std::chrono::seconds timeout_;
};

} // namespace fut::exec
