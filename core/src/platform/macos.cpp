// macOS: the .dylib suffix (CMake also writes MODULE libraries as .so, so both
// are accepted) and _NSGetExecutablePath. Everything else is in posix.cpp.

#include <mach-o/dyld.h>

#include <cstdint>
#include <string>

#include <fut/platform.hpp>

namespace fut::platform {

std::span<const std::string_view> library_suffixes() {
    static constexpr std::string_view suffixes[]{".dylib", ".so"};
    return suffixes;
}

std::filesystem::path executable_path() {
    std::uint32_t size = 0;
    _NSGetExecutablePath(nullptr, &size);           // asks for the length
    std::string buf(size, '\0');
    if (_NSGetExecutablePath(buf.data(), &size) != 0) return {};
    std::error_code ec;
    const auto canonical = std::filesystem::canonical(buf.c_str(), ec);
    return ec ? std::filesystem::path{buf} : canonical;
}

} // namespace fut::platform
