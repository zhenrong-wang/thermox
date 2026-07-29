find_package(OpenSSL QUIET)
if(NOT OpenSSL_FOUND)
    message(STATUS
        "OpenSSL not found; object artifact adapters disabled")
    return()
endif()

add_library(thermox_object_artifacts
    src/result_artifact_store.cpp
)
add_library(
    thermox::object_artifacts ALIAS thermox_object_artifacts)
target_include_directories(thermox_object_artifacts
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)
target_link_libraries(thermox_object_artifacts
    PUBLIC thermox_service
    PRIVATE OpenSSL::Crypto
)
target_compile_features(thermox_object_artifacts PUBLIC cxx_std_20)
target_compile_options(thermox_object_artifacts PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall -Wextra -Wpedantic>
)

add_executable(thermox_object_artifact_tests
    tests/object_artifact_tests.cpp
)
target_link_libraries(thermox_object_artifact_tests PRIVATE
    thermox_object_artifacts
)
add_test(
    NAME thermox_object_artifact_tests
    COMMAND thermox_object_artifact_tests
)

set(CURL_NO_CURL_CMAKE ON)
find_package(CURL QUIET)
if(NOT CURL_FOUND)
    message(STATUS
        "libcurl/OpenSSL not found; S3-compatible driver disabled")
    return()
endif()

add_library(thermox_s3_object_store
    src/s3_compatible_object_store.cpp
)
add_library(
    thermox::s3_object_store ALIAS thermox_s3_object_store)
target_include_directories(thermox_s3_object_store
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)
target_link_libraries(thermox_s3_object_store
    PUBLIC thermox_object_artifacts
    PRIVATE CURL::libcurl
)
target_compile_features(thermox_s3_object_store PUBLIC cxx_std_20)
target_compile_options(thermox_s3_object_store PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall -Wextra -Wpedantic>
)

add_executable(thermox_s3_object_store_tests
    tests/s3_object_store_tests.cpp
)
target_link_libraries(thermox_s3_object_store_tests PRIVATE
    thermox_s3_object_store
)
add_test(
    NAME thermox_s3_object_store_tests
    COMMAND thermox_s3_object_store_tests
)
set_tests_properties(thermox_s3_object_store_tests PROPERTIES
    SKIP_RETURN_CODE 77
    LABELS integration
)
