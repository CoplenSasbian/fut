# fut_server -- the thin shell: parse the arguments, hand them to the library,
# block until SIGINT/SIGTERM. All the machinery lives in fut_core.
add_executable(fut_server main.cpp)
target_link_libraries(fut_server PRIVATE fut::core)

# The executable sits next to the core library; on ELF/Mach-O it must find
# it without LD_LIBRARY_PATH -- both it and the app libraries use $ORIGIN.
if(NOT WIN32)
    set_target_properties(fut_server PROPERTIES
        BUILD_RPATH "$ORIGIN"
        INSTALL_RPATH "$ORIGIN"
    )
endif()

if(WIN32)
    # main.cpp touches asio's io_context, so it needs Winsock at link time.
    target_link_libraries(fut_server PRIVATE ws2_32)
endif()
