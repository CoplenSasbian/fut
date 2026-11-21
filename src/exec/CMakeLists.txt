# fut_server -- the server executable (rarely rebuilt; business updates are
# plain DLL swaps inside bin/apps).
add_executable(fut_server
    main.cpp
    builder.cpp
    server.cpp
    router.cpp
    app_loader.cpp
    config.cpp
    http_message.cpp
    http_parser.cpp
    log.cpp
)

target_include_directories(fut_server PRIVATE
    ${PROJECT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(fut_server PRIVATE
    Boost::asio
    llhttp::llhttp_static
    glaze::glaze
)

if(WIN32)
    # ws2_32: Winsock2; mswsock: AcceptEx 等 Winsock 扩展
    target_link_libraries(fut_server PRIVATE ws2_32 mswsock)
endif()
