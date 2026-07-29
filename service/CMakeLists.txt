add_library(thermox_service
    src/in_memory_artifacts.cpp
    src/in_memory_jobs.cpp
    src/serialization.cpp
    src/simulation_jobs.cpp
    src/simulation_runtime.cpp
    src/simulation_service.cpp
)
add_library(thermox::service ALIAS thermox_service)

target_include_directories(thermox_service
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)
target_link_libraries(thermox_service PRIVATE thermox_platform)
if(TARGET thermox_cantera_backend)
    target_link_libraries(
        thermox_service PRIVATE thermox_cantera_backend)
    target_compile_definitions(
        thermox_service PRIVATE THERMOX_HAS_CANTERA_BACKEND=1)
endif()
target_compile_features(thermox_service PUBLIC cxx_std_20)
target_compile_definitions(thermox_service PRIVATE
    THERMOX_PLATFORM_VERSION="${PROJECT_VERSION}"
)
target_compile_options(thermox_service PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall -Wextra -Wpedantic>
)

add_library(thermox_service_native INTERFACE)
add_library(thermox::service_native ALIAS thermox_service_native)
target_link_libraries(thermox_service_native INTERFACE
    thermox_service
    thermox_platform
)
