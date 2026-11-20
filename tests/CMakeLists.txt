# ---- doctest 测试套件 ----

# vcpkg 生成的 doctestConfig.cmake 只定义 target；这里补上官方的用例发现脚本
if(NOT COMMAND doctest_discover_tests)
    include("${doctest_DIR}/doctest.cmake")
endif()

# 路由：纯逻辑
add_executable(test_router test_router.cpp ${PROJECT_SOURCE_DIR}/src/exec/router.cpp)
target_include_directories(test_router PRIVATE
    ${PROJECT_SOURCE_DIR}/include
    ${PROJECT_SOURCE_DIR}/src/exec
)
target_link_libraries(test_router PRIVATE doctest::doctest Boost::asio)
doctest_discover_tests(test_router)

# C++26 std::meta 反射
add_executable(test_reflect test_reflect.cpp)
target_include_directories(test_reflect PRIVATE ${PROJECT_SOURCE_DIR}/include)
target_link_libraries(test_reflect PRIVATE doctest::doctest)
doctest_discover_tests(test_reflect)

# 全链路：app_loader 加载 fut_app_admin / fut_app_web DLL，驱动协程调用 handle
add_executable(test_apps test_apps.cpp ${PROJECT_SOURCE_DIR}/src/exec/app_loader.cpp)
target_include_directories(test_apps PRIVATE
    ${PROJECT_SOURCE_DIR}/include
    ${PROJECT_SOURCE_DIR}/src/exec
)
target_link_libraries(test_apps PRIVATE doctest::doctest Boost::asio glaze::glaze)
if(WIN32)
    target_link_libraries(test_apps PRIVATE ws2_32)
endif()
doctest_discover_tests(test_apps
    PROPERTIES ENVIRONMENT "FUT_APPS_DIR=$<TARGET_FILE_DIR:fut_app_admin>"
)
