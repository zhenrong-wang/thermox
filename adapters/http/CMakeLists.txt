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

find_package(Threads REQUIRED)
add_executable(thermox_http_server
    apps/thermox_http_server.cpp
)
target_link_libraries(thermox_http_server PRIVATE
    thermox_http_api
    Threads::Threads
)
if(TARGET thermox_postgres_jobs)
    target_link_libraries(
        thermox_http_server PRIVATE thermox_postgres_jobs)
    target_compile_definitions(
        thermox_http_server PRIVATE THERMOX_HAS_POSTGRES_JOBS=1)
endif()
if(TARGET thermox_object_artifacts)
    target_link_libraries(
        thermox_http_server PRIVATE thermox_object_artifacts)
    target_compile_definitions(
        thermox_http_server PRIVATE
        THERMOX_HAS_OBJECT_ARTIFACTS=1)
endif()
if(TARGET thermox_s3_object_store)
    target_link_libraries(
        thermox_http_server PRIVATE thermox_s3_object_store)
    target_compile_definitions(
        thermox_http_server PRIVATE THERMOX_HAS_S3_OBJECT_STORE=1)
endif()
target_compile_definitions(thermox_http_server PRIVATE
    BOOST_ERROR_CODE_HEADER_ONLY
)
target_compile_features(thermox_http_server PRIVATE cxx_std_20)
target_compile_options(thermox_http_server PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall -Wextra -Wpedantic>
)
