#pragma once

// The request used directly -- no binding annotations -- and the response the
// connection hands over.

#include "../common.hpp"
#include "../dto.hpp"

namespace demo {

struct [[=fut::controller("/raw")]] raw_controller {

    [[=fut::http_get("/echo")]]
    asio::awaitable<void> echo(const fut::request& req, fut::response& out);

    [[=fut::http_post("/items/{id}")]]
    asio::awaitable<void> create(const fut::request& req, const fut::path_match& params,
                                 fut::response& out);

    [[=fut::http_get("/created")]]
    asio::awaitable<void> created(fut::response& out);
};

} // namespace demo
