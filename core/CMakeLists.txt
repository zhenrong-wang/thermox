cmake_minimum_required(VERSION 3.20)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    project(thermox VERSION 0.1.0 LANGUAGES CXX)
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
add_library(thermox_core
    src/dae_equation_system.cpp
    src/dense_linear_solver.cpp
    src/equation_system.cpp
    src/nonlinear_solver.cpp
    src/sparse_linear_solver.cpp
    src/sparse_matrix.cpp
    src/transient_solver.cpp
    src/variable_registry.cpp
)

target_include_directories(thermox_core
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)

target_compile_options(thermox_core PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall -Wextra -Wpedantic>
)

add_executable(thermox_core_tests tests/core_tests.cpp)
target_link_libraries(thermox_core_tests PRIVATE thermox_core)

add_executable(thermox_transient_tests tests/transient_tests.cpp)
target_link_libraries(thermox_transient_tests PRIVATE thermox_core)

enable_testing()
add_test(NAME thermox_core_tests COMMAND thermox_core_tests)
add_test(NAME thermox_transient_tests COMMAND thermox_transient_tests)
