cmake_minimum_required(VERSION 3.20)

project(thermox VERSION 0.2.0 LANGUAGES C CXX)

enable_testing()
set(THERMOX_CO2_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(THERMOX_IF97_BUILD_TESTS OFF CACHE BOOL "" FORCE)
add_subdirectory(modules/properties/co2)
add_subdirectory(modules/properties/water-steam-if97)
add_subdirectory(physics)
add_subdirectory(core)

add_executable(thermox_property_tests physics/tests/property_tests.cpp)
target_link_libraries(thermox_property_tests PRIVATE thermox_physics thermox_core)
add_test(NAME thermox_property_tests COMMAND thermox_property_tests)
