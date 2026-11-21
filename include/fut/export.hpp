#pragma once

// fut/export.hpp -- the DLL ABI surface shared by the executable and every
// module it loads. Nothing here knows what a module does.

#include <cstdint>

namespace fut {

// Bump when the fut_ctx layout or the app entry contract changes.
inline constexpr std::uint32_t abi_version = 2;

inline constexpr const char* library_version = "0.3.0";

#if defined(_WIN32)
#  define FUT_EXPORT __declspec(dllexport)
#else
#  define FUT_EXPORT __attribute__((visibility("default")))
#endif

} // namespace fut
