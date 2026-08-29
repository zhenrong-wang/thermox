add_library(thermox_platform
    src/assembly.cpp
    src/boundary_models.cpp
    src/calibration.cpp
    src/component_catalog.cpp
    src/component_registry.cpp
    src/correlation.cpp
    src/engineering_artifact.cpp
    src/expression_component.cpp
    src/control_models.cpp
    src/combustion_models.cpp
    src/drum_models.cpp
    src/dynamic_heat_exchanger_models.cpp
    src/dynamic_material_heat_exchanger_models.cpp
    src/dynamic_material_rigid_volume_models.cpp
    src/fluid_inventory_models.cpp
    src/heat_transfer_models.cpp
    src/material_turbomachinery_models.cpp
    src/model_document.cpp
    src/operating_envelope.cpp
    src/phase_separation_models.cpp
    src/performance_map.cpp
    src/power_models.cpp
    src/regime_map.cpp
    src/results.cpp
    src/storage_models.cpp
    src/turbomachinery_models.cpp
    src/transport_models.cpp
    src/two_phase_flow_groups.cpp
    src/unit_registry.cpp
    src/valve_models.cpp
    src/volumetric_expander_models.cpp
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
