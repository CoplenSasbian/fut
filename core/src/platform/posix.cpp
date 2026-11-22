// The POSIX implementation of the platform layer (fut/platform.hpp):
// dlopen, ANSI escapes and the *_r time functions. Linux and macOS share this;
// what differs between them (the library suffix, the way to ask for the
// executable) lives in linux.cpp and macos.cpp.

#include <dlfcn.h>

#include <cstdio>
#include <string>
#include <unistd.h>

#include <fut/platform.hpp>

namespace fut::platform {

library::~library() { close(); }

library& library::operator=(library&& other) noexcept {
    if (this != &other) {
        close();
        handle_ = std::exchange(other.handle_, nullptr);
    }
    return *this;
}

library library::open(const std::filesystem::path& path, std::string& error) {
    library lib;
    std::string native = path.string();
    // A bare name would make dlopen search the library path instead of the
    // working directory, so make a relative file name explicitly relative.
    if (native.find('/') == std::string::npos) native = "./" + native;
    lib.handle_ = dlopen(native.c_str(), RTLD_NOW | RTLD_LOCAL);
    if (!lib.handle_) {
        if (const char* why = dlerror()) error = why;
        else error = "unknown dlopen error";
    }
    return lib;
}

void* library::symbol(std::string_view name) const {
    if (!handle_) return nullptr;
    const std::string owned(name);
    return dlsym(handle_, owned.c_str());
}

void library::close() {
    if (!handle_) return;
    dlclose(handle_);
    handle_ = nullptr;
}

std::tm utc_time(std::time_t stamp) noexcept {
    std::tm value{};
    gmtime_r(&stamp, &value);
    return value;
}

std::tm local_time(std::time_t stamp) noexcept {
    std::tm value{};
    localtime_r(&stamp, &value);
    return value;
}

namespace {

std::string_view ansi_color(int severity) noexcept {
    switch (severity) {
    case 0: return "\x1b[90m";   // trace: grey
    case 1: return "\x1b[36m";   // debug: cyan
    case 2: return "\x1b[32m";   // info: green
    case 3: return "\x1b[33m";   // warning: yellow
    case 4: return "\x1b[31m";   // error: red
    case 5: return "\x1b[1;31m"; // critical: bold red
    default: return {};
    }
}

} // namespace

bool stdout_is_terminal() noexcept { return ::isatty(fileno(stdout)) != 0; }

void console_write(std::string_view text, int severity) noexcept {
    const std::string_view color = ansi_color(severity);
    if (color.empty() || !stdout_is_terminal()) {
        std::fwrite(text.data(), 1, text.size(), stdout);
        return;
    }
    std::fwrite(color.data(), 1, color.size(), stdout);
    std::fwrite(text.data(), 1, text.size(), stdout);
    static constexpr std::string_view reset = "\x1b[0m";
    std::fwrite(reset.data(), 1, reset.size(), stdout);
}

} // namespace fut::platform
