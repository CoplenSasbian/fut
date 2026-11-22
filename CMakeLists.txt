cmake_minimum_required(VERSION 3.25)

project(fut
    VERSION 0.2.0
    DESCRIPTION "C++26 coroutine web server library: server executable + annotation-routed app DLLs"
    LANGUAGES CXX)

# ---- global standard: C++26 ----
set(CMAKE_CXX_STANDARD 26)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

# ---- dependencies (vcpkg manifest mode, see vcpkg.json / CMakePresets.json) ----
find_package(Boost CONFIG REQUIRED COMPONENTS asio)
find_package(llhttp CONFIG REQUIRED)
find_package(glaze CONFIG REQUIRED)
find_package(doctest CONFIG REQUIRED)

# ---- C++26 static reflection (std::meta / P2996) is a HARD REQUIREMENT ----
if(NOT CMAKE_CXX_COMPILER_ID STREQUAL "GNU" OR CMAKE_CXX_COMPILER_VERSION VERSION_LESS 15)
    message(FATAL_ERROR
        "fut requires a compiler with C++26 std::meta reflection support "
        "(GCC >= 15, built with -freflection). There is no reflection-free "
        "fallback path.")
endif()
add_compile_options($<$<COMPILE_LANGUAGE:CXX>:-freflection>)

# Log call sites print file names relative to the project root.
add_compile_definitions(FUT_PROJECT_ROOT="${CMAKE_SOURCE_DIR}")

# ---- output layout: build/<preset>/bin/fut_server.exe + bin/apps/*.dll ----
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

# ---- bundle the MinGW runtime DLLs next to the binaries (self-contained bin/) ----
option(FUT_BUNDLE_RUNTIME "Copy compiler runtime DLLs into bin/" ON)
if(WIN32 AND FUT_BUNDLE_RUNTIME AND CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    get_filename_component(FUT_COMPILER_BIN "${CMAKE_CXX_COMPILER}" DIRECTORY)
    foreach(_rt libstdc++-6.dll libgcc_s_seh-1.dll libwinpthread-1.dll)
        if(EXISTS "${FUT_COMPILER_BIN}/${_rt}")
            file(COPY "${FUT_COMPILER_BIN}/${_rt}" DESTINATION "${CMAKE_BINARY_DIR}/bin")
        endif()
    endforeach()
endif()

option(FUT_BUILD_TESTS "Build the doctest suite" ON)

# Business-app helper: add_app(name, prefix, sources...)
include(${CMAKE_SOURCE_DIR}/cmake/fut.cmake)

add_subdirectory(core)
add_subdirectory(exec)
add_subdirectory(apps/demo)

fut_app_summary()

if(FUT_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
