#include "binding.hpp"

namespace demo {

namespace {

long long sum_to(int count) {
    long long sum = 0;
    for (int i = 1; i <= count; ++i) sum += i;
    return sum;
}

} // namespace

asio::awaitable<void> binding_controller::body(greeting_in in, fut::response& out) {
    out.dto(greeting_out{in.message, in.count * 2, sum_to(in.count)});
    co_return;
}

asio::awaitable<void> binding_controller::body_scalar(int count, fut::response& out) {
    out.dto(greeting_out{"scalar", count * 2, count});
    co_return;
}

asio::awaitable<void> binding_controller::form(greeting_in in, fut::response& out) {
    out.dto(greeting_out{in.message, in.count * 2, sum_to(in.count)});
    co_return;
}

asio::awaitable<void> binding_controller::upload(std::string title, fut::uploaded_file file,
                                                 fut::response& out) {
    out.dto(upload_out{std::move(title), std::string(file.filename), file.size(),
                       std::string(file.content_type)});
    co_return;
}

} // namespace demo
