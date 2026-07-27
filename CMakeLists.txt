cmake_minimum_required(VERSION 3.20)

project(thermox VERSION 0.2.0 LANGUAGES C CXX)

enable_testing()
set(THERMOX_CO2_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(THERMOX_IF97_BUILD_TESTS OFF CACHE BOOL "" FORCE)
add_subdirectory(modules/properties/co2)
add_subdirectory(modules/properties/water-steam-if97)
add_subdirectory(physics)
add_subdirectory(core)
add_subdirectory(platform)

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
target_link_libraries(thermox_cli PRIVATE thermox_platform)

add_executable(thermox_platform_tests platform/tests/platform_tests.cpp)
target_link_libraries(thermox_platform_tests PRIVATE thermox_platform)
target_compile_definitions(thermox_platform_tests PRIVATE
    THERMOX_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
)
add_test(NAME thermox_platform_tests COMMAND thermox_platform_tests)

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
