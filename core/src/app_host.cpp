#include <fut/exec/app_host.hpp>

#include <utility>

namespace fut::exec {

app_host::app_host(builder build) : build_(std::move(build)) {}

app_host::generation_ptr app_host::current() const {
    std::lock_guard<std::mutex> lock(m_);
    return current_;
}

app_host::generation_ptr app_host::reload(std::string& out_error) {
    generation_ptr fresh;
    try {
        fresh = build_(next_id_);
    } catch (const std::exception& e) {
        out_error = e.what();
        return nullptr;
    } catch (...) {
        out_error = "unknown error while building the next generation";
        return nullptr;
    }
    if (!fresh) {
        out_error = "the builder returned an empty generation";
        return nullptr;
    }

    generation_ptr previous;
    {
        std::lock_guard<std::mutex> lock(m_);
        previous = std::move(current_);
        current_ = fresh;          // the new one is live for requests
        ++next_id_;
    }
    // 'previous' is released here: if requests still hold it, the DLLs stay
    // loaded until the last of them finishes; otherwise this is the unload.
    return current();
}

std::size_t app_host::generation() const {
    std::lock_guard<std::mutex> lock(m_);
    return current_ ? current_->id : 0;
}

} // namespace fut::exec
