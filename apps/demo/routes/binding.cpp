#include "binding.hpp"

namespace demo {

namespace {

long long sum_to(int count) {
    long long sum = 0;
    for (int i = 1; i <= count; ++i) sum += i;
    return sum;
}

} // namespace

asio::awaitable<greeting_out> binding_controller::body(greeting_in in) {
    co_return greeting_out{in.message, in.count * 2, sum_to(in.count)};
}

asio::awaitable<greeting_out> binding_controller::body_scalar(int count) {
    co_return greeting_out{"scalar", count * 2, count};
}

asio::awaitable<greeting_out> binding_controller::form(greeting_in in) {
    co_return greeting_out{in.message, in.count * 2, sum_to(in.count)};
}

asio::awaitable<upload_out> binding_controller::upload(std::string title,
                                                       fut::uploaded_file file) {
    // The views are borrowed; store what must outlive the request as a copy.
    co_return upload_out{std::move(title), std::string(file.filename), file.size(),
                         std::string(file.content_type)};
}

} // namespace demo
