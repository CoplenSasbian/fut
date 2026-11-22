#pragma once

// Request-body binding: JSON bodies, form bodies, and an uploaded file.

#include "../common.hpp"
#include "../dto.hpp"

namespace demo {

struct [[=fut::controller{}]] binding_controller {

    [[=fut::http_post("/body")]]
    asio::awaitable<void> body([[=fut::from_body{}]] greeting_in in, fut::response& out);

    [[=fut::http_post("/body/scalar")]]
    asio::awaitable<void> body_scalar([[=fut::from_body{}]] int count, fut::response& out);

    [[=fut::http_post("/form")]]
    asio::awaitable<void> form([[=fut::from_form{}]] greeting_in in, fut::response& out);

    [[=fut::http_post("/upload")]]
    asio::awaitable<void> upload([[=fut::from_form{}]] std::string title,
                                 [[=fut::from_file{}]] fut::uploaded_file file,
                                 fut::response& out);
};

} // namespace demo
