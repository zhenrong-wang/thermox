add_library(thermox_service
    src/serialization.cpp
    src/simulation_service.cpp
)
add_library(thermox::service ALIAS thermox_service)

target_include_directories(thermox_service
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)
target_link_libraries(thermox_service PRIVATE thermox_platform)
target_compile_features(thermox_service PUBLIC cxx_std_20)
target_compile_options(thermox_service PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall -Wextra -Wpedantic>
)
