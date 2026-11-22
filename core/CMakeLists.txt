# fut_core -- the library, and this directory is all of it:
#
#   core/include/fut/**           every public header: http, log, scan, ctx, exec/*
#   core/src/*.cpp                the implementation
#   core/src/*.hpp                private pieces (listener, HTTP message layer)
#
# Both the server executable and every app DLL link this one shared library, so
# the log core, the router implementation and the module registry exist once in
# the process. There is exactly one library directory -- this one.
add_library(fut_core SHARED
    src/log.cpp
    src/router.cpp
    src/app_loader.cpp
    src/config.cpp
    src/server.cpp
    src/listener.cpp src/app_host.cpp
    src/http_parser.cpp
    src/http_message.cpp
)

add_library(fut::core ALIAS fut_core)

target_compile_definitions(fut_core PRIVATE FUT_CORE_BUILD)

target_include_directories(fut_core PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)

target_link_libraries(fut_core
    PUBLIC  Boost::asio glaze::glaze
    PRIVATE llhttp::llhttp_static
)

if(WIN32)
    # Winsock; mswsock for the Winsock extensions (AcceptEx and friends).
    target_link_libraries(fut_core PRIVATE ws2_32 mswsock)
endif()

# The DLL next to the executable, next to the test binaries, next to the apps.
set_target_properties(fut_core PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)
