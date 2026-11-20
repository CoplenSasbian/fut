#pragma once
// fut/app_entry.hpp —— 每个 app 动态库用 FUT_IMPLEMENT_APP(AppType)
// 生成三个 C 导出：fut_abi_version / fut_app_create / fut_app_destroy。
// exec 的 app_loader 通过 GetProcAddress / dlsym 解析它们。

#include <new>
#include <fut/version.hpp>
#include <fut/app.hpp>

#if defined(_WIN32)
#  define FUT_EXPORT __declspec(dllexport)
#else
#  define FUT_EXPORT __attribute__((visibility("default")))
#endif

#define FUT_IMPLEMENT_APP(APP_TYPE)                                          \
  extern "C" {                                                                \
  FUT_EXPORT std::uint32_t fut_abi_version() {                                \
      return ::fut::abi_version;                                              \
  }                                                                           \
  FUT_EXPORT ::fut::app* fut_app_create() {                                   \
      return new (std::nothrow) APP_TYPE();                                    \
  }                                                                           \
  FUT_EXPORT void fut_app_destroy(::fut::app* a) { delete a; }                \
  }
