add_library(thermox_physics
    src/co2_package.cpp
    src/coolprop_backend.cpp
    src/ideal_gas_package.cpp
    src/if97_package.cpp
    src/property_registry.cpp
    src/thermochemistry.cpp
)
add_library(thermox::physics ALIAS thermox_physics)
target_include_directories(thermox_physics PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)
target_include_directories(thermox_physics SYSTEM PRIVATE
    ${PROJECT_SOURCE_DIR}/modules/properties/coolprop/include
)
target_link_libraries(thermox_physics PRIVATE thermox_coolprop)
target_compile_features(thermox_physics PUBLIC cxx_std_20)
target_compile_options(thermox_physics PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall -Wextra -Wpedantic>
)

option(
    THERMOX_ENABLE_CANTERA
    "Build the optional Cantera thermochemistry backend"
    OFF
)
if(THERMOX_ENABLE_CANTERA)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(
        CANTERA
        REQUIRED
        IMPORTED_TARGET
        cantera>=3.2
    )
    add_library(
        thermox_cantera_backend
        src/cantera_thermochemistry.cpp
    )
    add_library(
        thermox::cantera_backend
        ALIAS thermox_cantera_backend
    )
    target_include_directories(
        thermox_cantera_backend
        PUBLIC
            $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
            $<INSTALL_INTERFACE:include>
    )
    target_link_libraries(
        thermox_cantera_backend
        PUBLIC thermox_physics
        PRIVATE PkgConfig::CANTERA
    )
    target_compile_features(
        thermox_cantera_backend PUBLIC cxx_std_20
    )
endif()
