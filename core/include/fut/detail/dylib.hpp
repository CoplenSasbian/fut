#pragma once

// fut/detail/dylib.hpp -- loading a shared library, once, for every platform.
//
// Windows: LoadLibraryW / GetProcAddress / FreeLibrary.
// Linux and macOS: dlopen / dlsym / dlclose.
//
// What the rest of the server cares about: open() reports the platform error,
// symbol() returns nullptr for a missing name (exported functions in an app
// are optional except fut_app_register), and the destructor unloads. Nothing
// else leaks into the ABI.

#include <array>
#include <cctype>
#include <cstddef>
#include <filesystem>
#include <string>
#include <string_view>
#include <utility>

#if defined(_WIN32)
// This header may be included before asio's, so keep <windows.h> from pulling
// in the old Winsock (asio insists on winsock2.h being first) and from
// defining min/max as macros.
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  include <windows.h>
#else
#  include <dlfcn.h>
#endif

namespace fut::detail {

// The suffixes a business app may have: .dll on Windows, .so on Linux,
// .dylib on macOS (CMake writes MODULE libraries as .so there too, so
// both are accepted).
inline constexpr std::array<std::string_view, 3> library_suffixes{
    ".dll", ".so", ".dylib"};

inline std::string ascii_lower(std::string_view s) {
    std::string out;
    out.reserve(s.size());
    for (const char c : s)
        out.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
    return out;
}

// True when the file name looks like a loadable app library. The test is on
// the suffix only -- a non-fut .so is opened and skipped by the loader.
inline bool has_library_suffix(std::string_view name) {
    const std::string lower = ascii_lower(name);
    for (const std::string_view suffix : library_suffixes)
        if (lower.size() >= suffix.size() &&
            lower.compare(lower.size() - suffix.size(), suffix.size(), suffix) == 0)
            return true;
    return false;
}

// The platform's last error, as text.
inline std::string dylib_error() {
#if defined(_WIN32)
    const DWORD code = GetLastError();
    return "error " + std::to_string(static_cast<unsigned long>(code));
#else
    const char* e = dlerror();
    return e ? std::string(e) : std::string("unknown dlopen error");
#endif
}

class shared_library {
public:
    shared_library() = default;
    ~shared_library() { close(); }

    shared_library(shared_library&& other) noexcept
        : handle_(std::exchange(other.handle_, nullptr)) {}
    shared_library& operator=(shared_library&& other) noexcept {
        if (this != &other) {
            close();
            handle_ = std::exchange(other.handle_, nullptr);
        }
        return *this;
    }
    shared_library(const shared_library&) = delete;
    shared_library& operator=(const shared_library&) = delete;

    // Opens a library. On failure the handle is empty and why says what the
    // platform reported.
    static shared_library open(const std::filesystem::path& path, std::string& why) {
        shared_library lib;
#if defined(_WIN32)
        // The wide API: an apps directory may contain non-ASCII characters.
        lib.handle_ = reinterpret_cast<void*>(LoadLibraryW(path.wstring().c_str()));
        if (!lib.handle_) why = dylib_error();
#else
        // dlopen with a bare name would search the library path, so make a
        // relative file name explicitly relative to the working directory.
        std::string native = path.string();
        if (native.find('/') == std::string::npos) native = "./" + native;
        lib.handle_ = dlopen(native.c_str(), RTLD_NOW | RTLD_LOCAL);
        if (!lib.handle_) why = dylib_error();
#endif
        return lib;
    }

    // A symbol, or nullptr when the library does not export it.
    [[nodiscard]] void* symbol(std::string_view name) const {
        if (!handle_) return nullptr;
        const std::string owned(name);   // the API wants NUL-terminated
#if defined(_WIN32)
        return reinterpret_cast<void*>(
            GetProcAddress(reinterpret_cast<HMODULE>(handle_), owned.c_str()));
#else
        return dlsym(handle_, owned.c_str());
#endif
    }

    void close() {
        if (!handle_) return;
#if defined(_WIN32)
        FreeLibrary(reinterpret_cast<HMODULE>(handle_));
#else
        dlclose(handle_);
#endif
        handle_ = nullptr;
    }

    [[nodiscard]] bool valid() const { return handle_ != nullptr; }
    [[nodiscard]] void* handle() const { return handle_; }

private:
    void* handle_ = nullptr;
};

} // namespace fut::detail
