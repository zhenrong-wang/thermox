cmake_minimum_required(VERSION 3.20)

project(thermox VERSION 0.2.0 LANGUAGES C CXX)

enable_testing()
set(THERMOX_CO2_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(THERMOX_IF97_BUILD_TESTS OFF CACHE BOOL "" FORCE)
add_subdirectory(modules/properties/co2)
add_subdirectory(modules/properties/water-steam-if97)
add_subdirectory(core)
add_subdirectory(physics)
