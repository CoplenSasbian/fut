#include "http_parser.hpp"

#include <cstring>
#include <vector>

#include <llhttp.h>

namespace fut::exec {

namespace {

// The two budgets the parser enforces before anything is buffered. Generous
// for real headers, small enough that a header bomb is not an out-of-memory.
constexpr std::size_t max_header_bytes = 64 * 1024;
constexpr std::size_t max_target_bytes = 8 * 1024;

bool iequals_ascii(std::string_view a, std::string_view b) {
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i) {
        auto ca = static_cast<unsigned char>(a[i]);
        auto cb = static_cast<unsigned char>(b[i]);
        if (std::tolower(ca) != std::tolower(cb)) return false;
    }
    return true;
}

} // namespace

struct request_parser::impl {
    llhttp_t parser{};
    llhttp_settings_t settings{};
    http_request_message message{};
    // Offsets into message.header_bytes while a field/value is being appended.
    // The views are materialised in on_headers_complete, once the buffer has
    // stopped growing: appending may reallocate it, and a view taken earlier
    // would then point at freed storage.
    struct span_offset {
        std::size_t begin = 0;
        std::size_t end = 0;
    };
    std::vector<span_offset> field_spans{};
    std::vector<span_offset> value_spans{};
    std::size_t field_start = 0;
    std::size_t value_start = 0;
    bool in_field_name = false;
    bool in_field_value = false;
    bool complete{false};
    bool paused_at_headers{false};
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
        paused_at_headers = false;
        llhttp_init(&parser, HTTP_REQUEST, &settings);
        parser.data = this;
        message.clear();
        field_spans.clear();   // capacity kept: no allocation after warm-up
        value_spans.clear();
        field_start = 0;
        value_start = 0;
        in_field_name = false;
        in_field_value = false;
        complete = false;
        consumed = 0;
        error.clear();
    }

    static impl* self(llhttp_t* p) { return static_cast<impl*>(p->data); }

    static int on_url(llhttp_t* p, const char* at, std::size_t length) {
        self(p)->message.target.append(at, length);
        return 0;
    }

    // A field name may arrive in pieces, and so may its value. Both are
    // appended to message.header_bytes as they come and only offsets are kept,
    // so the whole header block is one reused buffer and the views point
    // straight into it. A header is closed when the next field starts, or when
    // the header block ends.
    void close_header() {
        const std::size_t size = message.header_bytes.size();
        const std::size_t field_end = in_field_value ? value_start : size;
        field_spans.push_back({field_start, field_end});
        value_spans.push_back({in_field_value ? value_start : size, size});
        in_field_name = false;
        in_field_value = false;
    }

    // Turns the recorded offsets into views over the finished header block.
    void build_header_views() {
        message.headers.clear();
        const std::string_view all{message.header_bytes};
        message.headers.reserve(field_spans.size());
        for (std::size_t i = 0; i < field_spans.size(); ++i) {
            const span_offset f = field_spans[i];
            const span_offset v = value_spans[i];
            message.headers.emplace_back(all.substr(f.begin, f.end - f.begin),
                                         all.substr(v.begin, v.end - v.begin));
        }
    }

    static int on_header_field(llhttp_t* p, const char* at, std::size_t length) {
        impl* s = self(p);
        if (!s->in_field_name) {
            if (s->in_field_value) s->close_header();       // the previous one is done
            s->field_start = s->message.header_bytes.size();
            s->in_field_name = true;
            s->in_field_value = false;
        }
        s->message.header_bytes.append(at, length);
        return 0;
    }

    static int on_header_value(llhttp_t* p, const char* at, std::size_t length) {
        impl* s = self(p);
        if (!s->in_field_value) {
            s->in_field_name = false;   // the name is complete, the value starts here
            s->value_start = s->message.header_bytes.size();
            s->in_field_value = true;
        }
        s->message.header_bytes.append(at, length);
        return 0;
    }

    static int on_headers_complete(llhttp_t* p) {
        impl* s = self(p);
        if (s->in_field_name || s->in_field_value)   // the last header
            s->close_header();
        s->build_header_views();

        // Everything the dispatcher needs, before any early return below.
        s->message.keep_alive = llhttp_should_keep_alive(p) != 0;
        s->message.method = llhttp_method_name(static_cast<llhttp_method_t>(p->method));
        s->message.version = "HTTP/" + std::to_string(p->http_major) + "." +
                             std::to_string(p->http_minor);

        // Budgets: a header bomb or an absurd target must not be buffered.
        s->message.headers_too_large = s->message.header_bytes.size() > max_header_bytes;
        s->message.target_too_large = s->message.target.size() > max_target_bytes;
        if (s->message.headers_too_large || s->message.target_too_large) {
            s->message.body.clear();
            return HPE_PAUSED;   // stop: nothing more is read for this message
        }

        // A client may ask to be told before it sends a large body: stop at the
        // end of the header block, let the connection answer, then resume.
        for (const auto& [name, value] : s->message.headers)
            if (iequals_ascii(name, "expect") && iequals_ascii(value, "100-continue"))
                s->message.expect_continue = true;
        if (s->message.expect_continue) return HPE_PAUSED;
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

    if (err == HPE_PAUSED) {   // stopped on purpose: headers done, or message done
        const char* stop = llhttp_get_error_pos(&impl_->parser);
        impl_->consumed = static_cast<std::size_t>(stop - data);
        if (!impl_->complete) {
            impl_->paused_at_headers = true;
            return status::interim;
        }
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

void request_parser::resume() {
    if (!impl_->paused_at_headers) return;
    impl_->paused_at_headers = false;
    llhttp_resume(&impl_->parser);
}

const http_request_message& request_parser::message() const { return impl_->message; }
std::size_t request_parser::consumed() const { return impl_->consumed; }
std::string_view request_parser::error() const { return impl_->error; }

void request_parser::reset() { impl_->reset(); }

} // namespace fut::exec
