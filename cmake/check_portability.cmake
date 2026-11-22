# check_portability.cmake -- fails the build if an operating system leaked
# outside the platform layer.
#
#   cmake -DROOT=<source dir> -P cmake/check_portability.cmake
#
# The rule from docs/platform-layer.md: fut/platform.hpp and
# core/src/platform/* are the only places that may ask what system they run
# on. Everything else is portable C++, and this check keeps it that way on
# every platform, including the one being developed on.

if(NOT DEFINED ROOT)
    message(FATAL_ERROR "check_portability: pass -DROOT=<source dir>")
endif()

# Tokens that mean "I am talking about a specific operating system".
set(patterns "_WIN32" "__APPLE__" "__linux__" "_MSC_VER" "unistd\\.h" "windows\\.h")

file(GLOB_RECURSE sources
    "${ROOT}/core/src/*.cpp"
    "${ROOT}/core/src/*.hpp"
    "${ROOT}/core/include/*.hpp"
    "${ROOT}/exec/*.cpp"
    "${ROOT}/apps/*.cpp"
    "${ROOT}/apps/*.hpp"
)

set(violations "")
foreach(file IN LISTS sources)
    # The platform layer itself is exactly where these belong.
    if(file MATCHES "/core/src/platform/")
        continue()
    endif()
    # export.hpp speaks to compilers (dllexport vs visibility), not to systems.
    if(file MATCHES "/fut/export\\.hpp$")
        continue()
    endif()

    file(READ "${file}" content)
    foreach(pattern IN LISTS patterns)
        if(content MATCHES "${pattern}")
            string(APPEND violations "  ${file}: ${pattern}\n")
        endif()
    endforeach()
endforeach()

if(violations)
    message(FATAL_ERROR
        "Operating system code outside the platform layer:\n${violations}\n"
        "Move it behind fut/platform.hpp (docs/platform-layer.md) -- every"
        "other file must build on any system.")
endif()

message(STATUS "check_portability: no OS-specific code outside the platform layer")
