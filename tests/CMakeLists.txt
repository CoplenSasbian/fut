# ---- doctest suite ----

# The vcpkg-generated doctestConfig.cmake only defines the target; pull in the
# official per-case discovery script as well.
if(NOT COMMAND doctest_discover_tests)
    include("${doctest_DIR}/doctest.cmake")
endif()

# Logging: reflection-based level names, chains, sinks (the log core is in fut_core)
add_executable(test_log test_log.cpp)
target_include_directories(test_log PRIVATE ${PROJECT_SOURCE_DIR}/core/include)
target_link_libraries(test_log PRIVATE doctest::doctest fut::core glaze::glaze)
doctest_discover_tests(test_log)

# Route registry: registration, matching, specificity
add_executable(test_router test_router.cpp)
target_include_directories(test_router PRIVATE ${PROJECT_SOURCE_DIR}/core/include)
target_link_libraries(test_router PRIVATE doctest::doctest fut::core Boost::asio)
if(WIN32)
    target_link_libraries(test_router PRIVATE ws2_32)
endif()
doctest_discover_tests(test_router)

# C++26 std::meta 反射
add_executable(test_reflect test_reflect.cpp)
target_include_directories(test_reflect PRIVATE ${PROJECT_SOURCE_DIR}/core/include)
target_link_libraries(test_reflect PRIVATE doctest::doctest)
doctest_discover_tests(test_reflect)

# Form bodies: urlencoded + multipart parsing
add_executable(test_form test_form.cpp)
target_include_directories(test_form PRIVATE ${PROJECT_SOURCE_DIR}/core/include)
target_link_libraries(test_form PRIVATE doctest::doctest Boost::asio glaze::glaze)
if(WIN32)
    target_link_libraries(test_form PRIVATE ws2_32)
endif()
doctest_discover_tests(test_form)

# HTTP message layer: llhttp parsing + response serialization
# The message layer is internal to fut_core, so this test compiles its sources
# directly instead of going through the library's exported symbols.
add_executable(test_http_parser test_http_parser.cpp
    ${PROJECT_SOURCE_DIR}/core/src/http_parser.cpp
    ${PROJECT_SOURCE_DIR}/core/src/http_message.cpp
)
target_include_directories(test_http_parser PRIVATE
    ${PROJECT_SOURCE_DIR}/core/include
    ${PROJECT_SOURCE_DIR}/core/src
)
target_link_libraries(test_http_parser PRIVATE doctest::doctest llhttp::llhttp_static)
doctest_discover_tests(test_http_parser)

# Config file: the log chain list is a user-facing interface
add_executable(test_config test_config.cpp)
target_include_directories(test_config PRIVATE ${PROJECT_SOURCE_DIR}/core/include)
target_link_libraries(test_config PRIVATE doctest::doctest fut::core glaze::glaze)
doctest_discover_tests(test_config)

# C++26 scanner + controller registration + every binding style
add_executable(test_reflect_router test_reflect_router.cpp)
target_include_directories(test_reflect_router PRIVATE ${PROJECT_SOURCE_DIR}/core/include)
target_link_libraries(test_reflect_router PRIVATE doctest::doctest Boost::asio glaze::glaze)
if(WIN32)
    target_link_libraries(test_reflect_router PRIVATE ws2_32)
endif()
doctest_discover_tests(test_reflect_router)

# End-to-end: app_loader loads fut_app_demo, it registers into the router
# through fut_ctx, and its routes are driven like the server does.
add_executable(test_apps test_apps.cpp)
target_include_directories(test_apps PRIVATE ${PROJECT_SOURCE_DIR}/core/include)
target_link_libraries(test_apps PRIVATE doctest::doctest fut::core Boost::asio glaze::glaze)
add_dependencies(test_apps fut_app_demo)
if(WIN32)
    target_link_libraries(test_apps PRIVATE ws2_32)
endif()
doctest_discover_tests(test_apps
    PROPERTIES ENVIRONMENT "FUT_APPS_DIR=$<TARGET_FILE_DIR:fut_app_demo>"
)
