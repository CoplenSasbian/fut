#pragma once

// The request and the response are rich enough to write a whole service
// without a single binding annotation: take the request, read what you need,
// build the response.
//
//     const fut::request&  method, path, query, body, headers, remote
//     const fut::path_match&  the captured path segments
//
// Nothing below uses from_body / from_query / from_path / from_header.

#include "../common.hpp"
#include "../dto.hpp"

namespace demo {

struct [[=fut::controller("/raw")]] raw_controller {

    // Everything about the call, read straight off the request.
    [[=fut::http_get("/echo")]]
    asio::awaitable<fut::response> echo(const fut::request& req);

    // A JSON body parsed by hand, a path parameter, a query value, a header.
    [[=fut::http_post("/items/{id}")]]
    asio::awaitable<fut::response> create(const fut::request& req,
                                          const fut::path_match& params);

    // A response that is nothing but status + headers + body.
    [[=fut::http_get("/created")]]
    asio::awaitable<fut::response> created();
};

} // namespace demo
