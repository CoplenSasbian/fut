#pragma once
// fut/version.hpp —— exec 与 app 动态库共享的 ABI 版本。
// 接口（include/fut/）有任何不兼容变更时 bump fut::abi_version，
// exec 加载时会校验并拒绝加载 ABI 不符的旧 DLL。

#include <cstdint>

namespace fut {

inline constexpr std::uint32_t abi_version = 1;
inline constexpr const char* server_version = "0.1.0";

} // namespace fut
