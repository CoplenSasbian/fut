#include "http_parser.hpp"

#include <cstring>

#include <llhttp.h>

namespace fut::exec {

struct request_parser::impl {
    llhttp_t parser{};
    llhttp_settings_t settings{};
    http_request_message message{};
    std::string pending_field{};      // field name awaiting its value
    bool in_field_name{false};
    bool complete{false};
    std::size_t consumed{0};
    std::string error{};

    impl() {
        llhttp_settings_init(&settings);
        settings.on_url = &impl::on_url;
        settings.on_header_field = &impl::on_header_field;
        settings.on_header_value = &impl::on_header_value;
        settings.on_headers_complete = &impl::on_headers_complete;
        settings.on_body = &impl::on_body;
        settings.on_message_complete = &impl::on_message_complete;
        llhttp_init(&parser, HTTP_REQUEST, &settings);
        parser.data = this;
    }

    void reset() {
        llhttp_init(&parser, HTTP_REQUEST, &settings);
        parser.data = this;
        message.clear();
        pending_field.clear();
        in_field_name = false;
        complete = false;
        consumed = 0;
        error.clear();
    }

    static impl* self(llhttp_t* p) { return static_cast<impl*>(p->data); }

    static int on_url(llhttp_t* p, const char* at, std::size_t length) {
        self(p)->message.target.append(at, length);
        return 0;
    }

    static int on_header_field(llhttp_t* p, const char* at, std::size_t length) {
        impl* s = self(p);
        if (s->in_field_name) {
            s->pending_field.append(at, length);   // field name arrives in pieces
        } else {
            s->pending_field.assign(at, length);
            s->in_field_name = true;
        }
        return 0;
    }

    static int on_header_value(llhttp_t* p, const char* at, std::size_t length) {
        impl* s = self(p);
        if (s->in_field_name) {
            s->message.headers.emplace_back(std::move(s->pending_field), std::string{});
            s->pending_field.clear();
            s->in_field_name = false;
        }
        s->message.headers.back().second.append(at, length);
        return 0;
    }

    static int on_headers_complete(llhttp_t* p) {
        impl* s = self(p);
        if (s->in_field_name) {   // header without a value
            s->message.headers.emplace_back(std::move(s->pending_field), std::string{});
            s->pending_field.clear();
            s->in_field_name = false;
        }
        s->message.keep_alive = llhttp_should_keep_alive(p) != 0;
        s->message.method = llhttp_method_name(static_cast<llhttp_method_t>(p->method));
        s->message.version = "HTTP/" + std::to_string(p->http_major) + "." +
                             std::to_string(p->http_minor);
        return 0;
    }

    static int on_body(llhttp_t* p, const char* at, std::size_t length) {
        self(p)->message.body.append(at, length);
        return 0;
    }

    static int on_message_complete(llhttp_t* p) {
        impl* s = self(p);
        s->complete = true;
        // Stop parsing here: anything after this belongs to the next request.
        return HPE_PAUSED;
    }
};

request_parser::request_parser() : impl_(std::make_unique<impl>()) {}
request_parser::~request_parser() = default;

request_parser::status request_parser::feed(const char* data, std::size_t length) {
    impl_->complete = false;
    impl_->consumed = 0;

    const llhttp_errno_t err = llhttp_execute(&impl_->parser, data, length);

    if (err == HPE_PAUSED) {   // message complete, parsing stopped on purpose
        const char* stop = llhttp_get_error_pos(&impl_->parser);
        impl_->consumed = static_cast<std::size_t>(stop - data);
        return status::complete;
    }
    if (err != HPE_OK) {
        impl_->error = llhttp_errno_name(err);
        if (const char* reason = llhttp_get_error_reason(&impl_->parser))
            impl_->error += std::string(": ") + reason;
        return status::error;
    }
    // No complete message in this chunk: llhttp keeps its state, the bytes
    // are consumed.
    impl_->consumed = length;
    return status::need_more;
}

const http_request_message& request_parser::message() const { return impl_->message; }
std::size_t request_parser::consumed() const { return impl_->consumed; }
std::string_view request_parser::error() const { return impl_->error; }

void request_parser::reset() { impl_->reset(); }

} // namespace fut::exec
