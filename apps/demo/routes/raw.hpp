#pragma once

// The request and the response, used directly -- no binding annotations.
//
//   const fut::request&   what arrived (a view; the connection owns the bytes)
//   const fut::path_match& the captured path segments
//
// The action returns a fut::response value: status, media type, headers, body.
// The connection takes it over (absorb) and its own response writes it -- an
// action only takes fut::response& when it wants to write the body itself
// (streaming, see routes/stream.*).

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

    // Status, media type, headers and a cookie, all on one value.
    [[=fut::http_get("/created")]]
    asio::awaitable<fut::response> created();
};

} // namespace demo
