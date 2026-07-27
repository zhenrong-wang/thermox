add_library(thermox_platform
    src/component_registry.cpp
    src/model_document.cpp
    src/results.cpp
)
add_library(thermox::platform ALIAS thermox_platform)
target_include_directories(thermox_platform
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)
target_link_libraries(thermox_platform PUBLIC thermox_core thermox_physics)
target_compile_features(thermox_platform PUBLIC cxx_std_20)
target_compile_options(thermox_platform PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall -Wextra -Wpedantic>
)
