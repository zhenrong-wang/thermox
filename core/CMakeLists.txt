cmake_minimum_required(VERSION 3.20)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    project(thermox VERSION 0.1.0 LANGUAGES CXX)
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(
    THERMOX_ENABLE_UMFPACK
    "Use SuiteSparse UMFPACK for the default sparse linear solve"
    OFF
)
add_library(thermox_core
    src/continuation_solver.cpp
    src/dae_equation_system.cpp
    src/dense_cholesky.cpp
    src/dense_linear_solver.cpp
    src/equation_system.cpp
    src/least_squares_solver.cpp
    src/nonlinear_solver.cpp
    src/solver_policy_benchmark.cpp
    src/sparse_linear_solver.cpp
    src/sparse_matrix.cpp
    src/transient_solver.cpp
    src/variable_registry.cpp
)
add_library(thermox::core ALIAS thermox_core)
set_target_properties(thermox_core PROPERTIES EXPORT_NAME core)

target_include_directories(thermox_core
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)

target_compile_options(thermox_core PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall -Wextra -Wpedantic>
)

if(THERMOX_ENABLE_UMFPACK)
    find_path(
        THERMOX_UMFPACK_INCLUDE_DIR
        NAMES umfpack.h
        PATH_SUFFIXES suitesparse
        REQUIRED
    )
    find_library(
        THERMOX_UMFPACK_LIBRARY
        NAMES umfpack
        REQUIRED
    )
    target_include_directories(
        thermox_core PRIVATE ${THERMOX_UMFPACK_INCLUDE_DIR}
    )
    target_link_libraries(
        thermox_core PRIVATE
            $<BUILD_INTERFACE:${THERMOX_UMFPACK_LIBRARY}>
            $<INSTALL_INTERFACE:thermox::umfpack_runtime>
    )
    target_compile_definitions(
        thermox_core PRIVATE THERMOX_HAS_UMFPACK=1
    )
endif()

add_executable(thermox_core_tests tests/core_tests.cpp)
target_link_libraries(thermox_core_tests PRIVATE thermox_core)

add_executable(thermox_transient_tests tests/transient_tests.cpp)
target_link_libraries(thermox_transient_tests PRIVATE thermox_core)

add_executable(
    thermox_solver_policy_benchmark_tests
    tests/solver_policy_benchmark_tests.cpp
)
target_link_libraries(
    thermox_solver_policy_benchmark_tests PRIVATE thermox_core
)

enable_testing()
add_test(NAME thermox_core_tests COMMAND thermox_core_tests)
add_test(NAME thermox_transient_tests COMMAND thermox_transient_tests)
add_test(
    NAME thermox_solver_policy_benchmark_tests
    COMMAND thermox_solver_policy_benchmark_tests
)
