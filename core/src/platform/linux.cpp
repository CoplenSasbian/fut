// Linux: the .so suffix and /proc/self/exe. Everything else is in posix.cpp.

#include <cerrno>
#include <string>
#include <unistd.h>

#include <fut/platform.hpp>

namespace fut::platform {

std::span<const std::string_view> library_suffixes() {
    static constexpr std::string_view suffixes[]{".so"};
    return suffixes;
}

std::filesystem::path executable_path() {
    std::string buf(4096, '\0');
    for (;;) {
        const ssize_t n = ::readlink("/proc/self/exe", buf.data(), buf.size());
        if (n < 0) return {};                       // not available here
        if (static_cast<std::size_t>(n) < buf.size()) {
            buf.resize(static_cast<std::size_t>(n));
            return std::filesystem::path{buf};
        }
        buf.resize(buf.size() * 2);                 // truncated: ask for more
    }
}

} // namespace fut::platform
