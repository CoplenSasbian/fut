# fut_server —— 主程序（很少需要重编译；业务更新走 bin/apps 下的 DLL 替换）
add_executable(fut_server
    main.cpp
    server.cpp
    router.cpp
    app_loader.cpp
    config.cpp
)

target_include_directories(fut_server PRIVATE
    ${PROJECT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(fut_server PRIVATE
    Boost::beast
    Boost::asio
    glaze::glaze
)

if(WIN32)
    # ws2_32: Winsock2; mswsock: AcceptEx 等 Winsock 扩展
    target_link_libraries(fut_server PRIVATE ws2_32 mswsock)
endif()
