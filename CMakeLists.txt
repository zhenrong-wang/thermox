cmake_minimum_required(VERSION 3.20)

project(thermox VERSION 0.2.0 LANGUAGES C CXX)

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

add_executable(thermox_service_tests service/tests/service_tests.cpp)
target_link_libraries(thermox_service_tests PRIVATE
    thermox_service_native
)
target_compile_definitions(thermox_service_tests PRIVATE
    THERMOX_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
)
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
add_test(NAME platform_rankine_example_cli
    COMMAND thermox_cli solve
        --model ${CMAKE_CURRENT_SOURCE_DIR}/core/examples/simple_rankine.json
        --case design
)

add_executable(thermox_property_tests physics/tests/property_tests.cpp)
target_link_libraries(thermox_property_tests PRIVATE thermox_physics thermox_core)
add_test(NAME thermox_property_tests COMMAND thermox_property_tests)
