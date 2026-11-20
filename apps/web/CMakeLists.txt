# /api/web —— 业务动态库 fut_app_web.dll（更新业务只需替换此文件）
add_library(fut_app_web MODULE web_app.cpp)

set_target_properties(fut_app_web PROPERTIES
    PREFIX ""
    OUTPUT_NAME "fut_app_web"
    LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin/apps
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin/apps
)

target_include_directories(fut_app_web PRIVATE ${PROJECT_SOURCE_DIR}/include)
target_link_libraries(fut_app_web PRIVATE Boost::asio glaze::glaze)
