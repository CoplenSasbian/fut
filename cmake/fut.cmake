# fut CMake helpers.
#
# add_app(<name> <prefix> [sources...])
#
# Creates one business app DLL: target fut_app_<name>, artifact
# fut_app_<name>.dll in <binary>/bin/apps (next to the server executable),
# with the standard include path and link set. The DLL is a MODULE whose
# exported table comes from FUT_EXPORT_APP in the sources.
#
#     add_app(admin /api/admin admin_app.cpp)     # -> fut_app_admin.dll
#
# Conventions enforced here:
#   * the prefix must be an absolute path segment and not end in '/' --
#     a malformed prefix would silently never match;
#   * MODULE targets are governed by LIBRARY_OUTPUT_DIRECTORY (Windows DLLs
#     included -- RUNTIME_OUTPUT_DIRECTORY alone is ignored for them);
#   * FUT_APP_NAME / FUT_APP_PREFIX are defined as quoted literals, so an
#     app may write FUT_EXPORT_APP(FUT_APP_NAME, FUT_APP_PREFIX, ...) instead
#     of repeating the strings.
#
# Every created app is recorded in the global property FUT_APPS as one
# "<target>:<prefix>" entry for summaries and tooling. (A ';' would split the
# CMake list and double the entry count.)

function(add_app name prefix)
    if(NOT prefix MATCHES "^/")
        message(FATAL_ERROR
            "add_app(${name}, '${prefix}'): the app prefix must start with '/' (e.g. '/api/admin')")
    endif()
    if(prefix MATCHES "/$")
        message(FATAL_ERROR
            "add_app(${name}, '${prefix}'): the app prefix must not end with '/'")
    endif()
    if(ARGC LESS 3)
        message(FATAL_ERROR
            "add_app(${name}, '${prefix}'): at least one source file is required")
    endif()

    set(target fut_app_${name})
    add_library(${target} MODULE ${ARGN})

    set_target_properties(${target} PROPERTIES
        PREFIX ""
        OUTPUT_NAME "${target}"
        LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin/apps"
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin/apps"
    )

    target_include_directories(${target} PRIVATE ${PROJECT_SOURCE_DIR}/include)
    target_link_libraries(${target} PRIVATE Boost::asio glaze::glaze)
    target_compile_definitions(${target} PRIVATE
        FUT_APP_NAME="${name}"
        FUT_APP_PREFIX="${prefix}"
    )

    set_property(GLOBAL APPEND PROPERTY FUT_APPS "${target}:${prefix}")

    message(STATUS "add_app: ${target} at ${prefix}")
endfunction()

# Prints how many business apps were registered. Call after every
# add_subdirectory that uses add_app(); reads the global FUT_APPS property.
function(fut_app_summary)
    get_property(apps GLOBAL PROPERTY FUT_APPS)
    if(apps)
        list(LENGTH apps count)
        message(STATUS "fut: ${count} business app(s) registered: ${apps}")
    else()
        message(STATUS "fut: no business apps registered")
    endif()
endfunction()
