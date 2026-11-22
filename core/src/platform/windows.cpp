// The Windows implementation of the platform layer (fut/platform.hpp).

#ifndef WIN32_LEAN_AND_MEAN
#  define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#  define NOMINMAX
#endif
#include <Windows.h>

#include <cstdio>
#include <string>
#include <vector>

#include <fut/platform.hpp>

namespace fut::platform {

std::span<const std::string_view> library_suffixes() {
    static constexpr std::string_view suffixes[]{".dll"};
    return suffixes;
}

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
    // The wide API: an apps directory may contain non-ASCII characters.
    lib.handle_ = reinterpret_cast<void*>(LoadLibraryW(path.wstring().c_str()));
    if (!lib.handle_)
        error = "error " + std::to_string(static_cast<unsigned long>(GetLastError()));
    return lib;
}

void* library::symbol(std::string_view name) const {
    if (!handle_) return nullptr;
    const std::string owned(name);   // the API wants NUL-terminated text
    return reinterpret_cast<void*>(
        GetProcAddress(reinterpret_cast<HMODULE>(handle_), owned.c_str()));
}

void library::close() {
    if (!handle_) return;
    FreeLibrary(reinterpret_cast<HMODULE>(handle_));
    handle_ = nullptr;
}

std::filesystem::path executable_path() {
    std::wstring buf(32768, L'\0');
    for (;;) {
        const DWORD n = GetModuleFileNameW(nullptr, buf.data(), static_cast<DWORD>(buf.size()));
        if (n == 0) return {};
        if (n < buf.size()) {
            buf.resize(n);
            return std::filesystem::path{buf};
        }
        buf.resize(buf.size() * 2);   // truncated: ask for more room
    }
}

std::tm utc_time(std::time_t stamp) noexcept {
    std::tm value{};
    gmtime_s(&value, &stamp);
    return value;
}

std::tm local_time(std::time_t stamp) noexcept {
    std::tm value{};
    localtime_s(&value, &stamp);
    return value;
}

namespace {

std::uint16_t severity_attribute(int severity) noexcept {
    switch (severity) {
    case 0: return FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE;   // trace
    case 1: return FOREGROUND_GREEN | FOREGROUND_BLUE;                    // debug
    case 2: return FOREGROUND_GREEN;                                      // info
    case 3: return FOREGROUND_RED | FOREGROUND_GREEN;                     // warning
    case 4: return FOREGROUND_RED;                                        // error
    case 5: return FOREGROUND_RED | FOREGROUND_INTENSITY;                 // critical
    default: return FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE;
    }
}

} // namespace

bool stdout_is_terminal() noexcept {
    const HANDLE out = GetStdHandle(STD_OUTPUT_HANDLE);
    if (out == INVALID_HANDLE_VALUE || out == nullptr) return false;
    DWORD mode = 0;
    return GetConsoleMode(out, &mode) != 0;
}

void console_write(std::string_view text, int severity) noexcept {
    const auto handle = GetStdHandle(STD_OUTPUT_HANDLE);
    CONSOLE_SCREEN_BUFFER_INFO saved{};
    const bool have_handle = handle != INVALID_HANDLE_VALUE && handle != nullptr;
    const bool have_saved = have_handle && GetConsoleScreenBufferInfo(handle, &saved) != 0;
    if (have_saved) SetConsoleTextAttribute(handle, severity_attribute(severity));
    std::fwrite(text.data(), 1, text.size(), stdout);
    if (have_saved) SetConsoleTextAttribute(handle, saved.wAttributes);
}

} // namespace fut::platform
