#pragma once

// fut/export.hpp -- the DLL ABI surface shared by the executable and every
// module it loads. Nothing here knows what a module does.

#include <cstdint>

// The core library's export surface. fut_core is a shared library: the server
// executable and every app DLL link the same one, which is what makes state
// such as the log chains and the router module genuinely shared.
#if defined(_WIN32)
#  if defined(FUT_CORE_BUILD)
#    define FUT_API __attribute__((dllexport))
#  else
#    define FUT_API __attribute__((dllimport))
#  endif
#elif defined(__GNUC__)
#  define FUT_API __attribute__((visibility("default")))
#else
#  define FUT_API
#endif

namespace fut {

// Bump when the fut_ctx layout or the app entry contract changes.
inline constexpr std::uint32_t abi_version = 5;

inline constexpr const char* library_version = "0.3.0";

#if defined(_WIN32)
#  define FUT_EXPORT __declspec(dllexport)
#else
#  define FUT_EXPORT __attribute__((visibility("default")))
#endif

} // namespace fut
