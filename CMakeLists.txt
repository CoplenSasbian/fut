cmake_minimum_required(VERSION 3.25)

project(fut
    VERSION 0.1.0
    DESCRIPTION "Asio/Beast C++26 协程 webserver：exec + 业务 app 动态库"
    LANGUAGES CXX)

# ---- 全局标准：C++26 ----
set(CMAKE_CXX_STANDARD 26)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "构建类型" FORCE)
endif()

# ---- 依赖（vcpkg manifest 模式，见 vcpkg.json / CMakePresets.json）----
find_package(Boost CONFIG REQUIRED COMPONENTS asio beast)
find_package(glaze CONFIG REQUIRED)
find_package(doctest CONFIG REQUIRED)

# ---- C++26 静态反射（std::meta / P2996）为硬性要求 ----
if(NOT CMAKE_CXX_COMPILER_ID STREQUAL "GNU" OR CMAKE_CXX_COMPILER_VERSION VERSION_LESS 15)
    message(FATAL_ERROR
        "fut 要求支持 C++26 std::meta 的编译器（GCC >= 15 且开启 -freflection）；"
        "本项目工具链为 D:/mingw64 的 GCC 16.2")
endif()
add_compile_options($<$<COMPILE_LANGUAGE:CXX>:-freflection>)

# ---- 输出布局：build/bin/fut_server.exe + build/bin/apps/*.dll ----
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

# ---- MinGW 运行时 DLL 伴随部署（bin/ 自包含，无需系统 PATH）----
option(FUT_BUNDLE_RUNTIME "把编译器运行时 DLL 复制到 bin/" ON)
if(WIN32 AND FUT_BUNDLE_RUNTIME AND CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    get_filename_component(FUT_COMPILER_BIN "${CMAKE_CXX_COMPILER}" DIRECTORY)
    foreach(_rt libstdc++-6.dll libgcc_s_seh-1.dll libwinpthread-1.dll)
        if(EXISTS "${FUT_COMPILER_BIN}/${_rt}")
            file(COPY "${FUT_COMPILER_BIN}/${_rt}" DESTINATION "${CMAKE_BINARY_DIR}/bin")
        endif()
    endforeach()
endif()

option(FUT_BUILD_TESTS "构建 doctest 测试" ON)

add_subdirectory(src/exec)
add_subdirectory(apps/admin)
add_subdirectory(apps/web)

if(FUT_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
