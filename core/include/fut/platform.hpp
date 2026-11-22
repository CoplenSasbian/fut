#pragma once

// fut/platform.hpp -- the platform layer: the only place in the library that
// talks to the operating system.

// Everything else is portable C++: a server, a router, a parser and a logger
// that would compile on any system with a C++26 compiler and Asio. What is
// left -- loading a shared library, finding the executable, reading the clock
// in the right time zone, colouring a console -- lives behind the declarations
// below, and has exactly one implementation per platform:

//   core/src/platform/windows.cpp   Win32: LoadLibraryW, GetModuleFileNameW,
//                                   gmtime_s, console attributes
//   core/src/platform/posix.cpp     dlopen, ANSI escapes, *_r time functions
//   core/src/platform/linux.cpp     the .so suffix, /proc/self/exe
//   core/src/platform/macos.cpp     the .dylib suffix, _NSGetExecutablePath

// Adding a platform means adding one file and one CMake branch;
// docs/platform-layer.md walks through it. No other file in the tree should
// need an #ifdef for an operating system.

#include <cstddef>
#include <cstdint>
#include <ctime>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <utility>

#include <fut/export.hpp>

namespace fut::platform {

// ---------------------------------------------------------------------------
// Shared libraries
// ---------------------------------------------------------------------------

// The suffixes a loadable app library has here: .dll, .so, .dylib.
[[nodiscard]] FUT_API std::span<const std::string_view> library_suffixes();

// True when the file name ends with one of those, case-insensitively. Only
// a prefilter: what makes a library an app is the fut_app_register symbol.
// Implemented here, once, for every platform.
inline bool is_library_name(std::string_view file_name) {
    for (const std::string_view suffix : library_suffixes()) {
        if (file_name.size() < suffix.size()) continue;
        const auto tail = file_name.substr(file_name.size() - suffix.size());
        bool same = true;
        for (std::size_t i = 0; i < suffix.size(); ++i) {
            const auto a = static_cast<unsigned char>(tail[i]);
            const auto b = static_cast<unsigned char>(suffix[i]);
            const auto lower = [](unsigned char c) {
                return static_cast<unsigned char>(c >= 'A' && c <= 'Z' ? c + 32 : c);
            };
            if (lower(a) != lower(b)) { same = false; break; }
        }
        if (same) return true;
    }
    return false;
}

// A loaded library, move-only, unloaded when destroyed. This is the whole
// difference between platforms: LoadLibrary/GetProcAddress/FreeLibrary
// against dlopen/dlsym/dlclose.
class FUT_API library {
public:
    library() = default;
    ~library();

    library(library&& other) noexcept : handle_(std::exchange(other.handle_, nullptr)) {}
    library& operator=(library&& other) noexcept;
    library(const library&) = delete;
    library& operator=(const library&) = delete;

    // Opens a library. On failure the handle is empty and error holds what
    // the platform reported (GetLastError / dlerror).
    [[nodiscard]] static library open(const std::filesystem::path& path, std::string& error);

    // A symbol, or nullptr when the library does not export it.
    [[nodiscard]] void* symbol(std::string_view name) const;

    void close();
    [[nodiscard]] bool valid() const { return handle_ != nullptr; }
    [[nodiscard]] void* handle() const { return handle_; }

private:
    void* handle_ = nullptr;
};

// ---------------------------------------------------------------------------
// Process
// ---------------------------------------------------------------------------

// The path of the running executable, or an empty path when the platform has
// no way to ask (the caller then falls back to the working directory).
[[nodiscard]] FUT_API std::filesystem::path executable_path();

// ---------------------------------------------------------------------------
// Time
// ---------------------------------------------------------------------------

// Broken-down time, thread-safe: gmtime_s / gmtime_r behind one name. The
// HTTP Date header is UTC; log lines are local.
[[nodiscard]] FUT_API std::tm utc_time(std::time_t stamp) noexcept;
[[nodiscard]] FUT_API std::tm local_time(std::time_t stamp) noexcept;

// ---------------------------------------------------------------------------
// Console
// ---------------------------------------------------------------------------

// True when stdout is a terminal, so a caller can skip colour for a pipe.
[[nodiscard]] FUT_API bool stdout_is_terminal() noexcept;

// Writes text to stdout coloured for a severity (0 = trace .. 5 = critical),
// restoring the previous attributes. Console attributes on Windows, ANSI
// escapes elsewhere; plain text when stdout is not a terminal.
FUT_API void console_write(std::string_view text, int severity) noexcept;

} // namespace fut::platform
