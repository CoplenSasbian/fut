#pragma once

// Request-body binding: JSON bodies, form bodies, and an uploaded file.
//
// The two "whole DTO" forms fill a struct member by member, by name; a scalar
// parameter is converted from the body or a single field.

#include "../common.hpp"

#include "../dto.hpp"

namespace demo {

struct [[=fut::controller{}]] binding_controller {

    // from_body on the whole DTO (JSON)
    [[=fut::http_post("/body")]]
    asio::awaitable<greeting_out> body([[=fut::from_body{}]] greeting_in in) {
        long long sum = 0;
        for (int i = 1; i <= in.count; ++i) sum += i;
        co_return greeting_out{in.message, in.count * 2, sum};
    }

    // from_body on a scalar: the body is just "21"
    [[=fut::http_post("/body/scalar")]]
    asio::awaitable<greeting_out> body_scalar([[=fut::from_body{}]] int count) {
        co_return greeting_out{"scalar", count * 2, count};
    }

    // from_form on the whole DTO: message=...&count=... fills greeting_in
    [[=fut::http_post("/form")]]
    asio::awaitable<greeting_out> form([[=fut::from_form{}]] greeting_in in) {
        long long sum = 0;
        for (int i = 1; i <= in.count; ++i) sum += i;
        co_return greeting_out{in.message, in.count * 2, sum};
    }

    // from_form on a single field, plus from_file for multipart/form-data
    [[=fut::http_post("/upload")]]
    asio::awaitable<upload_out> upload([[=fut::from_form{}]] std::string title,
                                       [[=fut::from_file{}]] fut::uploaded_file file) {
        co_return upload_out{title, file.filename, file.size(), file.content_type};
    }
};

} // namespace demo
