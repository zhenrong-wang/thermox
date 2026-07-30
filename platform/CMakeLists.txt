add_library(thermox_platform
    src/boundary_models.cpp
    src/calibration.cpp
    src/component_catalog.cpp
    src/component_registry.cpp
    src/control_models.cpp
    src/combustion_models.cpp
    src/fluid_inventory_models.cpp
    src/heat_transfer_models.cpp
    src/material_turbomachinery_models.cpp
    src/model_document.cpp
    src/performance_map.cpp
    src/power_models.cpp
    src/results.cpp
    src/storage_models.cpp
    src/turbomachinery_models.cpp
    src/transport_models.cpp
    src/unit_registry.cpp
)
add_library(thermox::platform ALIAS thermox_platform)
set_target_properties(
    thermox_platform PROPERTIES EXPORT_NAME platform)
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
