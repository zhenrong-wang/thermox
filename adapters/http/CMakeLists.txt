add_library(thermox_http_api
    src/http_api.cpp
)
add_library(thermox::http_api ALIAS thermox_http_api)

target_include_directories(thermox_http_api
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)
target_link_libraries(thermox_http_api PUBLIC thermox_service)
target_compile_features(thermox_http_api PUBLIC cxx_std_20)
target_compile_options(thermox_http_api PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall -Wextra -Wpedantic>
)

add_executable(thermox_http_api_tests
    tests/http_api_tests.cpp
)
target_link_libraries(thermox_http_api_tests PRIVATE thermox_http_api)
target_compile_definitions(thermox_http_api_tests PRIVATE
    THERMOX_SOURCE_DIR="${PROJECT_SOURCE_DIR}"
)
add_test(NAME thermox_http_api_tests COMMAND thermox_http_api_tests)

add_executable(thermox_api_server
    apps/thermox_api_server.cpp
)
target_link_libraries(thermox_api_server PRIVATE
    thermox_http_api
    thermox_host_runtime
)
target_compile_definitions(thermox_api_server PRIVATE
    BOOST_ERROR_CODE_HEADER_ONLY
)
target_compile_features(thermox_api_server PRIVATE cxx_std_20)
target_compile_options(thermox_api_server PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall -Wextra -Wpedantic>
)
