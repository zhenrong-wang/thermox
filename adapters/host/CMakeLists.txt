add_library(thermox_host_runtime
    src/host_runtime.cpp
)
add_library(thermox::host_runtime ALIAS thermox_host_runtime)
target_include_directories(thermox_host_runtime
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)
target_link_libraries(thermox_host_runtime PUBLIC thermox_service)
if(TARGET thermox_postgres_jobs)
    target_link_libraries(
        thermox_host_runtime PRIVATE thermox_postgres_jobs)
    target_compile_definitions(
        thermox_host_runtime PRIVATE THERMOX_HAS_POSTGRES_JOBS=1)
endif()
if(TARGET thermox_object_artifacts)
    target_link_libraries(
        thermox_host_runtime PRIVATE thermox_object_artifacts)
    target_compile_definitions(
        thermox_host_runtime PRIVATE
        THERMOX_HAS_OBJECT_ARTIFACTS=1)
endif()
if(TARGET thermox_s3_object_store)
    target_link_libraries(
        thermox_host_runtime PRIVATE thermox_s3_object_store)
    target_compile_definitions(
        thermox_host_runtime PRIVATE THERMOX_HAS_S3_OBJECT_STORE=1)
endif()
target_compile_features(thermox_host_runtime PUBLIC cxx_std_20)
target_compile_options(thermox_host_runtime PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall -Wextra -Wpedantic>
)

add_executable(thermox_worker
    apps/thermox_worker.cpp
)
target_link_libraries(thermox_worker PRIVATE thermox_host_runtime)
target_compile_features(thermox_worker PRIVATE cxx_std_20)
target_compile_options(thermox_worker PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall -Wextra -Wpedantic>
)
