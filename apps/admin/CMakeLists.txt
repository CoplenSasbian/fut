# /api/admin —— 业务动态库 fut_app_admin.dll（更新业务只需替换此文件）
add_library(fut_app_admin MODULE admin_app.cpp)

set_target_properties(fut_app_admin PROPERTIES
    PREFIX ""
    OUTPUT_NAME "fut_app_admin"
    # MODULE 目标输出目录由 LIBRARY_OUTPUT_DIRECTORY 决定（Windows DLL 亦然）
    LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin/apps
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin/apps
)

target_include_directories(fut_app_admin PRIVATE ${PROJECT_SOURCE_DIR}/include)
target_link_libraries(fut_app_admin PRIVATE Boost::asio glaze::glaze)
