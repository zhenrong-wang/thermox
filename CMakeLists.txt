cmake_minimum_required(VERSION 3.20)

project(thermox VERSION 0.2.0 LANGUAGES C CXX)

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

enable_testing()

set(COOLPROP_STATIC_LIBRARY ON CACHE BOOL "" FORCE)
set(COOLPROP_SHARED_LIBRARY OFF CACHE BOOL "" FORCE)
set(COOLPROP_OBJECT_LIBRARY OFF CACHE BOOL "" FORCE)
set(COOLPROP_NO_EXAMPLES ON CACHE BOOL "" FORCE)
set(COOLPROP_LIBRARY_NAME thermox_coolprop CACHE STRING "" FORCE)
set(COOLPROP_FPIC ON CACHE BOOL "" FORCE)
add_subdirectory(modules/properties/coolprop EXCLUDE_FROM_ALL)

add_subdirectory(physics)
add_subdirectory(core)
add_subdirectory(platform)
add_subdirectory(service)
add_subdirectory(adapters/postgres)
add_subdirectory(adapters/object_store)
add_subdirectory(adapters/host)
add_subdirectory(adapters/http)

add_library(thermox_examples
    core/example_models/src/brayton_cycle.cpp
    core/example_models/src/ideal_gas.cpp
)
target_include_directories(thermox_examples PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/core/example_models/include>
)
target_link_libraries(thermox_examples PUBLIC thermox_core thermox_platform)
target_compile_options(thermox_examples PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall -Wextra -Wpedantic>
)

add_executable(thermox_cli core/apps/thermox_cli.cpp)
target_link_libraries(thermox_cli PRIVATE thermox_service)

add_executable(thermox_platform_tests platform/tests/platform_tests.cpp)
target_link_libraries(thermox_platform_tests PRIVATE thermox_platform)
target_compile_definitions(thermox_platform_tests PRIVATE
    THERMOX_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
)
add_test(NAME thermox_platform_tests COMMAND thermox_platform_tests)

add_executable(
    thermox_unit_registry_tests
    platform/tests/unit_registry_tests.cpp)
target_link_libraries(
    thermox_unit_registry_tests PRIVATE thermox_platform)
add_test(
    NAME thermox_unit_registry_tests
    COMMAND thermox_unit_registry_tests)

add_executable(
    thermox_dynamic_component_tests
    platform/tests/dynamic_component_tests.cpp)
target_link_libraries(
    thermox_dynamic_component_tests PRIVATE thermox_platform)
add_test(
    NAME thermox_dynamic_component_tests
    COMMAND thermox_dynamic_component_tests)

add_executable(thermox_performance_map_tests
    platform/tests/performance_map_tests.cpp
)
target_link_libraries(thermox_performance_map_tests PRIVATE
    thermox_platform
)
add_test(
    NAME thermox_performance_map_tests
    COMMAND thermox_performance_map_tests
)

add_executable(thermox_correlation_tests
    platform/tests/correlation_tests.cpp
)
target_link_libraries(thermox_correlation_tests PRIVATE
    thermox_platform
)
add_test(
    NAME thermox_correlation_tests
    COMMAND thermox_correlation_tests
)

add_executable(
    thermox_expression_component_tests
    platform/tests/expression_component_tests.cpp)
target_link_libraries(
    thermox_expression_component_tests PRIVATE thermox_platform)
add_test(
    NAME thermox_expression_component_tests
    COMMAND thermox_expression_component_tests)

add_executable(thermox_service_tests service/tests/service_tests.cpp)
target_link_libraries(thermox_service_tests PRIVATE
    thermox_service_native
)
target_compile_definitions(thermox_service_tests PRIVATE
    THERMOX_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
)
if(TARGET thermox_cantera_backend)
    target_compile_definitions(
        thermox_service_tests PRIVATE
        THERMOX_TEST_HAS_CANTERA=1)
endif()
add_test(NAME thermox_service_tests COMMAND thermox_service_tests)

add_executable(thermox_job_service_tests
    service/tests/job_service_tests.cpp
)
target_link_libraries(thermox_job_service_tests PRIVATE
    thermox_service_native
)
target_compile_definitions(thermox_job_service_tests PRIVATE
    THERMOX_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
)
add_test(
    NAME thermox_job_service_tests
    COMMAND thermox_job_service_tests
)

add_executable(thermox_project_service_tests
    service/tests/project_service_tests.cpp
)
target_link_libraries(thermox_project_service_tests PRIVATE
    thermox_service
)
target_compile_definitions(thermox_project_service_tests PRIVATE
    THERMOX_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
)
add_test(
    NAME thermox_project_service_tests
    COMMAND thermox_project_service_tests
)

add_executable(thermox_example_tests core/example_models/tests/example_tests.cpp)
target_link_libraries(thermox_example_tests PRIVATE thermox_examples)
add_test(NAME thermox_example_tests COMMAND thermox_example_tests)

add_test(NAME platform_example_cli
    COMMAND thermox_cli solve --model ${CMAKE_CURRENT_SOURCE_DIR}/core/examples/air_compressor.json
)
add_test(NAME platform_transient_example_cli
    COMMAND thermox_cli simulate
        --model ${CMAKE_CURRENT_SOURCE_DIR}/core/examples/lumped_thermal_storage.json
        --case charge
        --end-time 10
)
add_test(NAME platform_closed_loop_drum_cli
    COMMAND thermox_cli simulate
        --model ${CMAKE_CURRENT_SOURCE_DIR}/core/examples/closed_loop_drum_control.json
        --case level_control
        --end-time 0.5
)
add_test(NAME platform_rankine_example_cli
    COMMAND thermox_cli solve
        --model ${CMAKE_CURRENT_SOURCE_DIR}/core/examples/simple_rankine.json
        --case design
)

add_executable(thermox_property_tests physics/tests/property_tests.cpp)
target_link_libraries(thermox_property_tests PRIVATE thermox_physics thermox_core)
add_test(NAME thermox_property_tests COMMAND thermox_property_tests)

set(THERMOX_SDK_TARGETS
    thermox_core
    thermox_physics
    thermox_platform
    thermox_service
    thermox_service_native
)
install(
    FILES $<TARGET_FILE:thermox_coolprop>
    DESTINATION ${CMAKE_INSTALL_LIBDIR}
)
if(TARGET thermox_cantera_backend)
    list(APPEND THERMOX_SDK_TARGETS thermox_cantera_backend)
endif()

install(
    TARGETS ${THERMOX_SDK_TARGETS}
    EXPORT ThermoxTargets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
install(
    DIRECTORY
        core/include/
        physics/include/
        platform/include/
        service/include/
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
    FILES_MATCHING PATTERN "*.hpp"
)
install(
    EXPORT ThermoxTargets
    FILE ThermoxTargets.cmake
    NAMESPACE thermox::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/Thermox
)

configure_package_config_file(
    cmake/ThermoxConfig.cmake.in
    ${CMAKE_CURRENT_BINARY_DIR}/ThermoxConfig.cmake
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/Thermox
)
write_basic_package_version_file(
    ${CMAKE_CURRENT_BINARY_DIR}/ThermoxConfigVersion.cmake
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion
)
install(
    FILES
        ${CMAKE_CURRENT_BINARY_DIR}/ThermoxConfig.cmake
        ${CMAKE_CURRENT_BINARY_DIR}/ThermoxConfigVersion.cmake
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/Thermox
)

set(THERMOX_SDK_STAGE_DIR
    ${CMAKE_CURRENT_BINARY_DIR}/sdk-stage)
set(THERMOX_SDK_CONFORMANCE_BUILD_DIR
    ${CMAKE_CURRENT_BINARY_DIR}/sdk-conformance)
set(THERMOX_SDK_CONFORMANCE_ENV)
if(THERMOX_ENABLE_CANTERA)
    list(APPEND THERMOX_SDK_CONFORMANCE_ENV
        PKG_CONFIG_PATH=${CANTERA_LIBDIR}/pkgconfig)
endif()
add_custom_target(
    thermox_sdk_conformance
    COMMAND
        ${CMAKE_COMMAND} -E rm -rf
        ${THERMOX_SDK_STAGE_DIR}
        ${THERMOX_SDK_CONFORMANCE_BUILD_DIR}
    COMMAND
        ${CMAKE_COMMAND} --install
        ${CMAKE_CURRENT_BINARY_DIR}
        --prefix ${THERMOX_SDK_STAGE_DIR}
    COMMAND
        ${CMAKE_COMMAND} -E env
        ${THERMOX_SDK_CONFORMANCE_ENV}
        ${CMAKE_COMMAND}
        -S ${CMAKE_CURRENT_SOURCE_DIR}/sdk/conformance
        -B ${THERMOX_SDK_CONFORMANCE_BUILD_DIR}
        -DCMAKE_PREFIX_PATH=${THERMOX_SDK_STAGE_DIR}
        -DCMAKE_BUILD_TYPE=Release
    COMMAND
        ${CMAKE_COMMAND} --build
        ${THERMOX_SDK_CONFORMANCE_BUILD_DIR}
        --parallel 1
    COMMAND
        ${THERMOX_SDK_CONFORMANCE_BUILD_DIR}/thermox_native_extension_conformance
    DEPENDS ${THERMOX_SDK_TARGETS}
    USES_TERMINAL
    COMMENT
        "Building the native extension fixture against staged Thermox SDK"
)
