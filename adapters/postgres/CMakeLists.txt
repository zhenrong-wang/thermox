find_package(PostgreSQL QUIET)

if(NOT PostgreSQL_FOUND)
    message(STATUS
        "PostgreSQL client library not found; PostgreSQL adapter disabled")
    return()
endif()

add_library(thermox_postgres_jobs
    src/job_codec.cpp
    src/postgres_job_repository.cpp
    src/postgres_project_repository.cpp
)
add_library(thermox::postgres_jobs ALIAS thermox_postgres_jobs)

target_include_directories(thermox_postgres_jobs
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)
target_link_libraries(thermox_postgres_jobs
    PUBLIC thermox_service
    PRIVATE PostgreSQL::PostgreSQL
)
target_compile_features(thermox_postgres_jobs PUBLIC cxx_std_20)
target_compile_options(thermox_postgres_jobs PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall -Wextra -Wpedantic>
)

add_executable(thermox_postgres_job_tests
    tests/postgres_job_tests.cpp
)
target_link_libraries(thermox_postgres_job_tests PRIVATE
    thermox_postgres_jobs
    PostgreSQL::PostgreSQL
)
target_compile_definitions(thermox_postgres_job_tests PRIVATE
    THERMOX_SOURCE_DIR="${PROJECT_SOURCE_DIR}"
)
add_test(
    NAME thermox_postgres_job_tests
    COMMAND thermox_postgres_job_tests
)
set_tests_properties(thermox_postgres_job_tests PROPERTIES
    SKIP_RETURN_CODE 77
    LABELS integration
)
