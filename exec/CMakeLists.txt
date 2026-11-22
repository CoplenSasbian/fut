# fut_server -- the thin shell: parse the arguments, hand them to the library,
# block until SIGINT/SIGTERM. All the machinery lives in fut_core.
add_executable(fut_server main.cpp)
target_link_libraries(fut_server PRIVATE fut::core)

if(WIN32)
    # main.cpp touches asio's io_context, so it needs Winsock at link time.
    target_link_libraries(fut_server PRIVATE ws2_32)
endif()
