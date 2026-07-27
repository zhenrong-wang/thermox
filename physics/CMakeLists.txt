add_library(thermox_physics
    src/co2_package.cpp
    src/ideal_gas_package.cpp
    src/if97_package.cpp
)
add_library(thermox::physics ALIAS thermox_physics)
target_include_directories(thermox_physics PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)
target_link_libraries(thermox_physics PUBLIC thermox::co2 thermox::if97)
target_compile_features(thermox_physics PUBLIC cxx_std_20)
target_compile_options(thermox_physics PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall -Wextra -Wpedantic>
)

add_executable(thermox_property_tests tests/property_tests.cpp)
target_link_libraries(thermox_property_tests PRIVATE thermox_physics thermox_core)
add_test(NAME thermox_property_tests COMMAND thermox_property_tests)
