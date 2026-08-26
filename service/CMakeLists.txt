find_package(OpenSSL REQUIRED COMPONENTS Crypto)

add_library(thermox_service
    src/artifact_declaration.cpp
    src/artifact_payload.cpp
    src/engineering_study.cpp
    src/in_memory_artifacts.cpp
    src/in_memory_jobs.cpp
    src/in_memory_projects.cpp
    src/iso2314_equivalent_cooling.cpp
    src/performance_test.cpp
    src/project_serialization.cpp
    src/projects.cpp
    src/result_comparison.cpp
    src/result_projection.cpp
    src/serialization.cpp
    src/simulation_jobs.cpp
    src/simulation_runtime.cpp
    src/simulation_service.cpp
    src/thermal_feasibility.cpp
    src/validation_evidence.cpp
    src/validation_series.cpp
)
add_library(thermox::service ALIAS thermox_service)
set_target_properties(
    thermox_service PROPERTIES EXPORT_NAME service)

target_include_directories(thermox_service
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
    PRIVATE
        ${CPM_PACKAGE_nlohmann_json_SOURCE_DIR}/include
)
target_link_libraries(thermox_service
    PUBLIC thermox_platform
    PRIVATE OpenSSL::Crypto
)
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
set_target_properties(
    thermox_service_native PROPERTIES EXPORT_NAME service_native)
target_link_libraries(thermox_service_native INTERFACE
    thermox_service
    thermox_platform
)
