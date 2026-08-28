cmake_minimum_required(VERSION 3.20)

project(thermox VERSION 0.2.0 LANGUAGES C CXX)

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

enable_testing()

set(COOLPROP_STATIC_LIBRARY ON CACHE BOOL "" FORCE)
set(COOLPROP_SHARED_LIBRARY OFF CACHE BOOL "" FORCE)
set(COOLPROP_OBJECT_LIBRARY OFF CACHE BOOL "" FORCE)
set(COOLPROP_NO_EXAMPLES ON CACHE BOOL "" FORCE)
set(COOLPROP_LIBRARY_NAME thermox_coolprop CACHE STRING "" FORCE)
set(COOLPROP_FPIC ON CACHE BOOL "" FORCE)
add_subdirectory(modules/properties/coolprop EXCLUDE_FROM_ALL)

add_subdirectory(physics)
add_subdirectory(core)
add_subdirectory(platform)
add_subdirectory(service)
add_subdirectory(adapters/postgres)
add_subdirectory(adapters/object_store)
add_subdirectory(adapters/host)
add_subdirectory(adapters/http)

add_library(thermox_examples
    core/example_models/src/brayton_cycle.cpp
    core/example_models/src/ideal_gas.cpp
)
target_include_directories(thermox_examples PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/core/example_models/include>
)
target_link_libraries(thermox_examples PUBLIC thermox_core thermox_platform)
target_compile_options(thermox_examples PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall -Wextra -Wpedantic>
)

add_executable(thermox_cli core/apps/thermox_cli.cpp)
target_link_libraries(thermox_cli PRIVATE thermox_service)
add_executable(
    thermox_orc_1kw_measured_state_audit
    benchmarks/orc_1kw/measured_state_audit.cpp)
target_link_libraries(
    thermox_orc_1kw_measured_state_audit PRIVATE thermox_physics)
target_compile_features(
    thermox_orc_1kw_measured_state_audit PRIVATE cxx_std_20)
target_compile_options(
    thermox_orc_1kw_measured_state_audit PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall -Wextra -Wpedantic>)
add_test(
    NAME benchmark_orc_1kw_measured_state_audit
    COMMAND thermox_orc_1kw_measured_state_audit
        ${CMAKE_CURRENT_SOURCE_DIR}/benchmarks/orc_1kw/measurements.csv)
add_test(
    NAME platform_iso2314_performance_test_cli
    COMMAND thermox_cli performance-test
        --input ${CMAKE_CURRENT_SOURCE_DIR}/examples/iso2314_performance_test.json
        --format json)
add_test(
    NAME platform_iso2314_equivalent_cooling_cli
    COMMAND thermox_cli iso2314-equivalent-cooling
        --input ${CMAKE_CURRENT_SOURCE_DIR}/examples/iso2314_equivalent_cooling.json
        --format json)
add_test(
    NAME platform_calibration_cli
    COMMAND thermox_cli calibrate
        --model ${CMAKE_CURRENT_SOURCE_DIR}/core/examples/air_compressor.json
        --calibration acceptance_fit
        --max-iterations 8
        --format json)
add_test(
    NAME platform_data_reconciliation_cli
    COMMAND thermox_cli reconcile
        --model ${CMAKE_CURRENT_SOURCE_DIR}/examples/data_reconciliation.json
        --reconciliation fixed_power_relaxed_airflow
        --max-iterations 6
        --format json)
add_test(
    NAME platform_weighted_reconciliation_cli
    COMMAND thermox_cli reconcile
        --model ${CMAKE_CURRENT_SOURCE_DIR}/examples/data_reconciliation.json
        --reconciliation weighted_repeated_power
        --mode weighted-measurements
        --max-iterations 6
        --format json)
add_test(
    NAME platform_small_signal_envelope_cli
    COMMAND thermox_cli linearize
        --model ${CMAKE_CURRENT_SOURCE_DIR}/core/examples/lumped_thermal_storage.json
        --case charge
        --input-variable heater.outlet.Q_dot
        --output-variable store.temperature
        --output-variable heater.outlet.Q_dot
        --verify-nonlinear-response
        --nonlinear-response-perturbation 1e-4
        --nonlinear-response-perturbation 3e-4
        --verify-nonlinear-trajectory
        --nonlinear-trajectory-perturbation 1e-4
        --nonlinear-trajectory-perturbation 3e-4
        --format json)
add_test(
    NAME platform_small_signal_model_family_cli
    COMMAND thermox_cli linearize-family
        --model ${CMAKE_CURRENT_SOURCE_DIR}/core/examples/dynamic_bidirectional_regime_spanning_rigid_volume.json
        --coordinate-name initial_specific_enthalpy
        --coordinate-dimension specific_energy
        --operating-point liquid_to_two_phase:760000:liquid
        --operating-point two_phase_to_liquid:758852.18418594659:two_phase
        --operating-point two_phase_to_vapor:2777000:two_phase
        --operating-point vapor_to_two_phase:2779486.9924528766:vapor
        --input-variable thermal_boundary.outlet.Q_dot
        --output-variable vessel.pressure
        --output-variable vessel.enthalpy
        --format json)
add_test(
    NAME platform_small_signal_model_family_evaluation_cli
    COMMAND thermox_cli linearize-family
        --model ${CMAKE_CURRENT_SOURCE_DIR}/core/examples/lumped_thermal_storage.json
        --coordinate-name inverse_thermal_capacity
        --coordinate-dimension temperature_per_energy
        --operating-point charge:6.666666666666667e-7:solid
        --operating-point charge_high_capacity:3.333333333333333e-7:solid
        --validation-point charge_nominal_capacity:5e-7:solid
        --maximum-interpolation-gap 4e-7
        --validation-absolute-tolerance 1e-14
        --validation-relative-tolerance 1e-10
        --evaluate-at 3.333333333333333e-7
        --evaluate-at 5e-7
        --input-variable heater.outlet.Q_dot
        --output-variable store.temperature
        --format json)

add_executable(thermox_platform_tests platform/tests/platform_tests.cpp)
target_link_libraries(thermox_platform_tests PRIVATE thermox_platform)
target_compile_definitions(thermox_platform_tests PRIVATE
    THERMOX_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
)
add_test(NAME thermox_platform_tests COMMAND thermox_platform_tests)

add_executable(
    thermox_unit_registry_tests
    platform/tests/unit_registry_tests.cpp)
target_link_libraries(
    thermox_unit_registry_tests PRIVATE thermox_platform)
add_test(
    NAME thermox_unit_registry_tests
    COMMAND thermox_unit_registry_tests)

add_executable(
    thermox_dynamic_component_tests
    platform/tests/dynamic_component_tests.cpp)
target_link_libraries(
    thermox_dynamic_component_tests PRIVATE thermox_platform)
add_test(
    NAME thermox_dynamic_component_tests
    COMMAND thermox_dynamic_component_tests)

add_executable(thermox_performance_map_tests
    platform/tests/performance_map_tests.cpp
)
target_link_libraries(thermox_performance_map_tests PRIVATE
    thermox_platform
)
add_test(
    NAME thermox_performance_map_tests
    COMMAND thermox_performance_map_tests
)

add_executable(thermox_correlation_tests
    platform/tests/correlation_tests.cpp
)
target_link_libraries(thermox_correlation_tests PRIVATE
    thermox_platform
)
target_compile_definitions(thermox_correlation_tests PRIVATE
    THERMOX_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
)
add_test(
    NAME thermox_correlation_tests
    COMMAND thermox_correlation_tests
)

add_executable(thermox_regime_map_tests
    platform/tests/regime_map_tests.cpp
)
target_link_libraries(thermox_regime_map_tests PRIVATE
    thermox_platform
)
add_test(
    NAME thermox_regime_map_tests
    COMMAND thermox_regime_map_tests
)

add_executable(
    thermox_expression_component_tests
    platform/tests/expression_component_tests.cpp)
target_link_libraries(
    thermox_expression_component_tests PRIVATE thermox_platform)
add_test(
    NAME thermox_expression_component_tests
    COMMAND thermox_expression_component_tests)

add_executable(thermox_service_tests service/tests/service_tests.cpp)
target_link_libraries(thermox_service_tests PRIVATE
    thermox_service_native
)
target_compile_definitions(thermox_service_tests PRIVATE
    THERMOX_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
)
if(TARGET thermox_cantera_backend)
    target_compile_definitions(
        thermox_service_tests PRIVATE
        THERMOX_TEST_HAS_CANTERA=1)
endif()
add_test(NAME thermox_service_tests COMMAND thermox_service_tests)
if(TARGET thermox_cantera_backend)
    set_tests_properties(thermox_service_tests PROPERTIES
        ENVIRONMENT "CANTERA_DATA=${CMAKE_CURRENT_SOURCE_DIR}")
endif()

add_executable(thermox_job_service_tests
    service/tests/job_service_tests.cpp
)
target_link_libraries(thermox_job_service_tests PRIVATE
    thermox_service_native
)
target_compile_definitions(thermox_job_service_tests PRIVATE
    THERMOX_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
)
add_test(
    NAME thermox_job_service_tests
    COMMAND thermox_job_service_tests
)

add_executable(thermox_project_service_tests
    service/tests/project_service_tests.cpp
)
target_link_libraries(thermox_project_service_tests PRIVATE
    thermox_service
)
target_compile_definitions(thermox_project_service_tests PRIVATE
    THERMOX_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
)
add_test(
    NAME thermox_project_service_tests
    COMMAND thermox_project_service_tests
)

add_executable(thermox_example_tests core/example_models/tests/example_tests.cpp)
target_link_libraries(thermox_example_tests PRIVATE thermox_examples)
add_test(NAME thermox_example_tests COMMAND thermox_example_tests)

add_test(NAME platform_example_cli
    COMMAND thermox_cli solve --model ${CMAKE_CURRENT_SOURCE_DIR}/core/examples/air_compressor.json
)
add_test(NAME platform_transient_example_cli
    COMMAND thermox_cli simulate
        --model ${CMAKE_CURRENT_SOURCE_DIR}/core/examples/lumped_thermal_storage.json
        --case charge
        --end-time 10
)
add_test(NAME platform_closed_loop_drum_cli
    COMMAND thermox_cli simulate
        --model ${CMAKE_CURRENT_SOURCE_DIR}/core/examples/closed_loop_drum_control.json
        --case level_control
        --end-time 0.5
)
add_test(NAME platform_dynamic_heat_exchanger_cli
    COMMAND thermox_cli simulate
        --model ${CMAKE_CURRENT_SOURCE_DIR}/core/examples/dynamic_heat_exchanger_cell.json
        --case heat_up
        --end-time 0.5
)
add_test(NAME platform_regime_spanning_heat_exchanger_cli
    COMMAND thermox_cli simulate
        --model ${CMAKE_CURRENT_SOURCE_DIR}/core/examples/regime_spanning_finite_volume_heat_exchanger.json
        --case boil_to_vapor
        --end-time 10
        --format json
)
set_tests_properties(platform_regime_spanning_heat_exchanger_cli PROPERTIES
    PASS_REGULAR_EXPRESSION
        "\"port_name\": \"cold_out\", \"domain\": \"fluid\", \"medium_id\": \"water\", \"phase\": \"vapor\""
)
add_test(NAME platform_regime_spanning_rigid_volume_cli
    COMMAND thermox_cli simulate
        --model ${CMAKE_CURRENT_SOURCE_DIR}/core/examples/dynamic_regime_spanning_rigid_volume.json
        --case two_phase_to_vapor
        --end-time 10
        --format json
)
set_tests_properties(platform_regime_spanning_rigid_volume_cli PROPERTIES
    PASS_REGULAR_EXPRESSION "\"phase\": \"vapor\""
)
foreach(regime_case IN ITEMS
        liquid_to_two_phase
        two_phase_to_liquid
        two_phase_to_vapor
        vapor_to_two_phase)
    if(regime_case MATCHES "liquid")
        set(regime_end_time 1)
    else()
        set(regime_end_time 10)
    endif()
    add_test(NAME platform_bidirectional_regime_${regime_case}_cli
        COMMAND thermox_cli simulate
            --model ${CMAKE_CURRENT_SOURCE_DIR}/core/examples/dynamic_bidirectional_regime_spanning_rigid_volume.json
            --case ${regime_case}
            --end-time ${regime_end_time}
            --format json
    )
endforeach()
set_tests_properties(
    platform_bidirectional_regime_liquid_to_two_phase_cli
    platform_bidirectional_regime_vapor_to_two_phase_cli
    PROPERTIES PASS_REGULAR_EXPRESSION "\"phase\": \"two_phase\""
)
set_tests_properties(
    platform_bidirectional_regime_two_phase_to_liquid_cli
    PROPERTIES PASS_REGULAR_EXPRESSION "\"phase\": \"liquid\""
)
set_tests_properties(
    platform_bidirectional_regime_two_phase_to_vapor_cli
    PROPERTIES PASS_REGULAR_EXPRESSION "\"phase\": \"vapor\""
)
if(TARGET thermox_cantera_backend)
    add_test(NAME benchmark_nasa_tmats_hpc_cli
        COMMAND thermox_cli solve
            --model ${CMAKE_CURRENT_SOURCE_DIR}/benchmarks/nasa_tmats/hpc_design_point.json
            --case published_design_point
            --performance-map ${CMAKE_CURRENT_SOURCE_DIR}/benchmarks/nasa_tmats/hpc_map.json
            --format json
    )
    set_tests_properties(benchmark_nasa_tmats_hpc_cli PROPERTIES
        PASS_REGULAR_EXPRESSION "\"status\": \"succeeded\"")
    add_test(NAME benchmark_nasa_agtf30_hpc_off_design_cli
        COMMAND thermox_cli solve
            --model ${CMAKE_CURRENT_SOURCE_DIR}/benchmarks/nasa_tmats/agtf30_hpc_off_design.json
            --case alt_30000_mach_072_2000
            --performance-map ${CMAKE_CURRENT_SOURCE_DIR}/benchmarks/nasa_tmats/agtf30_hpc_map.json
            --format json
    )
    set_tests_properties(
        benchmark_nasa_agtf30_hpc_off_design_cli PROPERTIES
        PASS_REGULAR_EXPRESSION "\"status\": \"succeeded\"")
    add_test(NAME benchmark_nasa_agtf30_hpt_off_design_cli
        COMMAND thermox_cli solve
            --model ${CMAKE_CURRENT_SOURCE_DIR}/benchmarks/nasa_tmats/agtf30_hpt_off_design.json
            --case alt_30000_mach_072_2000
            --performance-map ${CMAKE_CURRENT_SOURCE_DIR}/benchmarks/nasa_tmats/agtf30_hpt_map.json
            --format json
    )
    set_tests_properties(
        benchmark_nasa_agtf30_hpt_off_design_cli PROPERTIES
        PASS_REGULAR_EXPRESSION "\"status\": \"succeeded\"")
    add_test(NAME benchmark_nasa_agtf30_high_spool_cli
        COMMAND thermox_cli solve
            --model ${CMAKE_CURRENT_SOURCE_DIR}/benchmarks/nasa_tmats/agtf30_high_spool.json
            --case alt_30000_mach_072_2000
            --performance-map ${CMAKE_CURRENT_SOURCE_DIR}/benchmarks/nasa_tmats/agtf30_hpc_map.json
            --performance-map ${CMAKE_CURRENT_SOURCE_DIR}/benchmarks/nasa_tmats/agtf30_hpt_map.json
            --format json
    )
    set_tests_properties(
        benchmark_nasa_agtf30_high_spool_cli PROPERTIES
        PASS_REGULAR_EXPRESSION "\"status\": \"succeeded\""
        ENVIRONMENT "CANTERA_DATA=${CMAKE_CURRENT_SOURCE_DIR}")
    add_test(NAME benchmark_nasa_agtf30_low_spool_cli
        COMMAND thermox_cli solve
            --model ${CMAKE_CURRENT_SOURCE_DIR}/benchmarks/nasa_tmats/agtf30_low_spool.json
            --case alt_30000_mach_072_2000
            --performance-map ${CMAKE_CURRENT_SOURCE_DIR}/benchmarks/nasa_tmats/agtf30_fan_map.json
            --performance-map ${CMAKE_CURRENT_SOURCE_DIR}/benchmarks/nasa_tmats/agtf30_lpc_map.json
            --performance-map ${CMAKE_CURRENT_SOURCE_DIR}/benchmarks/nasa_tmats/agtf30_lpt_map.json
            --format json
    )
    set_tests_properties(
        benchmark_nasa_agtf30_low_spool_cli PROPERTIES
        PASS_REGULAR_EXPRESSION "\"status\": \"succeeded\""
        ENVIRONMENT "CANTERA_DATA=${CMAKE_CURRENT_SOURCE_DIR}")
    add_test(NAME benchmark_nasa_agtf30_continuous_twin_spool_cli
        COMMAND thermox_cli solve
            --model ${CMAKE_CURRENT_SOURCE_DIR}/benchmarks/nasa_tmats/agtf30_continuous_twin_spool.json
            --case alt_30000_mach_072_2000
            --performance-map ${CMAKE_CURRENT_SOURCE_DIR}/benchmarks/nasa_tmats/agtf30_fan_map.json
            --performance-map ${CMAKE_CURRENT_SOURCE_DIR}/benchmarks/nasa_tmats/agtf30_lpc_map.json
            --performance-map ${CMAKE_CURRENT_SOURCE_DIR}/benchmarks/nasa_tmats/agtf30_hpc_map.json
            --performance-map ${CMAKE_CURRENT_SOURCE_DIR}/benchmarks/nasa_tmats/agtf30_hpt_map.json
            --performance-map ${CMAKE_CURRENT_SOURCE_DIR}/benchmarks/nasa_tmats/agtf30_lpt_map.json
            --residual-tolerance 1e-9
            --format json
    )
    set_tests_properties(
        benchmark_nasa_agtf30_continuous_twin_spool_cli PROPERTIES
        PASS_REGULAR_EXPRESSION "\"status\": \"succeeded\""
        ENVIRONMENT "CANTERA_DATA=${CMAKE_CURRENT_SOURCE_DIR}")
    add_test(NAME benchmark_nasa_agtf30_ducted_twin_spool_cli
        COMMAND thermox_cli solve
            --model ${CMAKE_CURRENT_SOURCE_DIR}/benchmarks/nasa_tmats/agtf30_ducted_twin_spool.json
            --case alt_30000_mach_072_2000
            --performance-map ${CMAKE_CURRENT_SOURCE_DIR}/benchmarks/nasa_tmats/agtf30_fan_map.json
            --performance-map ${CMAKE_CURRENT_SOURCE_DIR}/benchmarks/nasa_tmats/agtf30_lpc_map.json
            --performance-map ${CMAKE_CURRENT_SOURCE_DIR}/benchmarks/nasa_tmats/agtf30_hpc_map.json
            --performance-map ${CMAKE_CURRENT_SOURCE_DIR}/benchmarks/nasa_tmats/agtf30_hpt_map.json
            --performance-map ${CMAKE_CURRENT_SOURCE_DIR}/benchmarks/nasa_tmats/agtf30_lpt_map.json
            --residual-tolerance 1e-9
            --format json
    )
    set_tests_properties(
        benchmark_nasa_agtf30_ducted_twin_spool_cli PROPERTIES
        PASS_REGULAR_EXPRESSION "\"status\": \"succeeded\""
        ENVIRONMENT "CANTERA_DATA=${CMAKE_CURRENT_SOURCE_DIR}")
    add_test(NAME benchmark_nasa_agtf30_nozzle_closed_twin_spool_cli
        COMMAND thermox_cli solve
            --model ${CMAKE_CURRENT_SOURCE_DIR}/benchmarks/nasa_tmats/agtf30_nozzle_closed_twin_spool.json
            --case alt_30000_mach_072_2000
            --performance-map ${CMAKE_CURRENT_SOURCE_DIR}/benchmarks/nasa_tmats/agtf30_fan_map.json
            --performance-map ${CMAKE_CURRENT_SOURCE_DIR}/benchmarks/nasa_tmats/agtf30_lpc_map.json
            --performance-map ${CMAKE_CURRENT_SOURCE_DIR}/benchmarks/nasa_tmats/agtf30_hpc_map.json
            --performance-map ${CMAKE_CURRENT_SOURCE_DIR}/benchmarks/nasa_tmats/agtf30_hpt_map.json
            --performance-map ${CMAKE_CURRENT_SOURCE_DIR}/benchmarks/nasa_tmats/agtf30_lpt_map.json
            --residual-tolerance 1e-9
            --format json
    )
    set_tests_properties(
        benchmark_nasa_agtf30_nozzle_closed_twin_spool_cli PROPERTIES
        PASS_REGULAR_EXPRESSION "\"status\": \"succeeded\""
        ENVIRONMENT "CANTERA_DATA=${CMAKE_CURRENT_SOURCE_DIR}")
    add_test(NAME benchmark_nasa_agtf30_vbv_nozzle_twin_spool_cli
        COMMAND thermox_cli solve
            --model ${CMAKE_CURRENT_SOURCE_DIR}/benchmarks/nasa_tmats/agtf30_vbv_nozzle_twin_spool.json
            --case alt_30000_mach_072_2000
            --performance-map ${CMAKE_CURRENT_SOURCE_DIR}/benchmarks/nasa_tmats/agtf30_fan_map.json
            --performance-map ${CMAKE_CURRENT_SOURCE_DIR}/benchmarks/nasa_tmats/agtf30_lpc_map.json
            --performance-map ${CMAKE_CURRENT_SOURCE_DIR}/benchmarks/nasa_tmats/agtf30_hpc_map.json
            --performance-map ${CMAKE_CURRENT_SOURCE_DIR}/benchmarks/nasa_tmats/agtf30_hpt_map.json
            --performance-map ${CMAKE_CURRENT_SOURCE_DIR}/benchmarks/nasa_tmats/agtf30_lpt_map.json
            --performance-map ${CMAKE_CURRENT_SOURCE_DIR}/benchmarks/nasa_tmats/agtf30_vbv_map.json
            --residual-tolerance 1e-9
            --format json
    )
    set_tests_properties(
        benchmark_nasa_agtf30_vbv_nozzle_twin_spool_cli PROPERTIES
        PASS_REGULAR_EXPRESSION "\"status\": \"succeeded\""
        ENVIRONMENT "CANTERA_DATA=${CMAKE_CURRENT_SOURCE_DIR}")
    add_test(NAME benchmark_nasa_agtf30_vbv_open_point_cli
        COMMAND thermox_cli solve
            --model ${CMAKE_CURRENT_SOURCE_DIR}/benchmarks/nasa_tmats/agtf30_vbv_open_point.json
            --case sea_level_static_1000
            --performance-map ${CMAKE_CURRENT_SOURCE_DIR}/benchmarks/nasa_tmats/agtf30_vbv_map.json
            --residual-tolerance 1e-9
            --format json
    )
    set_tests_properties(
        benchmark_nasa_agtf30_vbv_open_point_cli PROPERTIES
        PASS_REGULAR_EXPRESSION "\"status\": \"succeeded\""
        ENVIRONMENT "CANTERA_DATA=${CMAKE_CURRENT_SOURCE_DIR}")
    add_test(NAME benchmark_nasa_agtf30_vbv_bypass_branch_cli
        COMMAND thermox_cli solve
            --model ${CMAKE_CURRENT_SOURCE_DIR}/benchmarks/nasa_tmats/agtf30_vbv_bypass_branch.json
            --case sea_level_static_1000
            --performance-map ${CMAKE_CURRENT_SOURCE_DIR}/benchmarks/nasa_tmats/agtf30_vbv_map.json
            --globalization trust_region
            --residual-tolerance 1e-9
            --format json
    )
    set_tests_properties(
        benchmark_nasa_agtf30_vbv_bypass_branch_cli PROPERTIES
        PASS_REGULAR_EXPRESSION "\"status\": \"succeeded\""
        ENVIRONMENT "CANTERA_DATA=${CMAKE_CURRENT_SOURCE_DIR}")
    add_test(NAME benchmark_nasa_agtf30_core_exhaust_branch_cli
        COMMAND thermox_cli solve
            --model ${CMAKE_CURRENT_SOURCE_DIR}/benchmarks/nasa_tmats/agtf30_core_exhaust_branch.json
            --case sea_level_static_1000
            --globalization trust_region
            --residual-tolerance 1e-9
            --format json
    )
    set_tests_properties(
        benchmark_nasa_agtf30_core_exhaust_branch_cli PROPERTIES
        PASS_REGULAR_EXPRESSION "\"status\": \"succeeded\""
        ENVIRONMENT "CANTERA_DATA=${CMAKE_CURRENT_SOURCE_DIR}")
    add_test(NAME benchmark_nasa_agtf30_vbv_nozzle_static_cli
        COMMAND thermox_cli solve
            --model ${CMAKE_CURRENT_SOURCE_DIR}/benchmarks/nasa_tmats/agtf30_vbv_nozzle_static.json
            --case sea_level_static_1000
            --performance-map ${CMAKE_CURRENT_SOURCE_DIR}/benchmarks/nasa_tmats/agtf30_fan_map.json
            --performance-map ${CMAKE_CURRENT_SOURCE_DIR}/benchmarks/nasa_tmats/agtf30_lpc_map.json
            --performance-map ${CMAKE_CURRENT_SOURCE_DIR}/benchmarks/nasa_tmats/agtf30_hpc_map.json
            --performance-map ${CMAKE_CURRENT_SOURCE_DIR}/benchmarks/nasa_tmats/agtf30_hpt_map.json
            --performance-map ${CMAKE_CURRENT_SOURCE_DIR}/benchmarks/nasa_tmats/agtf30_lpt_map.json
            --performance-map ${CMAKE_CURRENT_SOURCE_DIR}/benchmarks/nasa_tmats/agtf30_vbv_map.json
            --continuation
            --continuation-initial-step 0.01
            --continuation-minimum-step 0.0001
            --residual-tolerance 1e-9
            --format json
    )
    set_tests_properties(
        benchmark_nasa_agtf30_vbv_nozzle_static_cli PROPERTIES
        PASS_REGULAR_EXPRESSION "\"status\": \"succeeded\""
        ENVIRONMENT "CANTERA_DATA=${CMAKE_CURRENT_SOURCE_DIR}")
    add_test(NAME platform_cooled_turbine_stage_cli
        COMMAND thermox_cli solve
            --model ${CMAKE_CURRENT_SOURCE_DIR}/core/examples/cooled_turbine_stage_cantera.json
            --case design
            --continuation
            --format json
    )
    set_tests_properties(platform_cooled_turbine_stage_cli PROPERTIES
        PASS_REGULAR_EXPRESSION
            "\"net_boundary_mass_flow\".*\"value_si\": 0"
    )
    add_test(NAME platform_dynamic_material_fluid_hrsg_cli
        COMMAND thermox_cli simulate
            --model ${CMAKE_CURRENT_SOURCE_DIR}/core/examples/dynamic_exhaust_water_hrsg_cell.json
            --case heat_up
            --end-time 0.1
    )
    add_test(NAME platform_distributed_material_fluid_steady_cli
        COMMAND thermox_cli solve
            --model ${CMAKE_CURRENT_SOURCE_DIR}/core/examples/two_cell_counterflow_exhaust_water.json
            --case steady
    )
    add_test(NAME platform_distributed_material_fluid_transient_cli
        COMMAND thermox_cli simulate
            --model ${CMAKE_CURRENT_SOURCE_DIR}/core/examples/two_cell_counterflow_exhaust_water.json
            --case heat_up
            --end-time 0.1
    )
    add_test(NAME platform_dynamic_two_phase_cell_cli
        COMMAND thermox_cli simulate
            --model ${CMAKE_CURRENT_SOURCE_DIR}/core/examples/dynamic_two_phase_rigid_volume_cell.json
            --case boil
            --end-time 1.0
    )
    add_test(NAME platform_dynamic_single_pressure_hrsg_cli
        COMMAND thermox_cli simulate
            --model ${CMAKE_CURRENT_SOURCE_DIR}/core/examples/dynamic_single_pressure_hrsg.json
            --case startup_segment
            --end-time 0.1
    )
    add_test(NAME platform_dynamic_forced_circulation_cli
        COMMAND thermox_cli simulate
            --model ${CMAKE_CURRENT_SOURCE_DIR}/core/examples/dynamic_forced_circulation_evaporator.json
            --case circulation_segment
            --end-time 0.1
    )
    add_test(NAME platform_dynamic_natural_circulation_cli
        COMMAND thermox_cli simulate
            --model ${CMAKE_CURRENT_SOURCE_DIR}/core/examples/dynamic_natural_circulation_evaporator.json
            --case buoyancy_segment
            --end-time 0.1
    )
    add_test(NAME platform_dynamic_distributed_two_phase_riser_cli
        COMMAND thermox_cli simulate
            --model ${CMAKE_CURRENT_SOURCE_DIR}/core/examples/dynamic_distributed_two_phase_riser.json
            --case heatup_segment
            --end-time 0.1
    )
    add_test(NAME platform_segmented_triple_pressure_hrsg_cli
        COMMAND thermox_cli solve
            --model ${CMAKE_CURRENT_SOURCE_DIR}/benchmarks/netl_b31a/segmented_hrsg.json
            --case published_boundary_decomposition
            --continuation
    )
endif()
add_test(NAME platform_two_cell_counterflow_steady_cli
    COMMAND thermox_cli solve
        --model ${CMAKE_CURRENT_SOURCE_DIR}/core/examples/two_cell_counterflow_heat_exchanger.json
        --case steady
)
add_test(NAME platform_two_cell_counterflow_transient_cli
    COMMAND thermox_cli simulate
        --model ${CMAKE_CURRENT_SOURCE_DIR}/core/examples/two_cell_counterflow_heat_exchanger.json
        --case heat_up
        --end-time 0.5
)
add_test(NAME platform_rankine_example_cli
    COMMAND thermox_cli solve
        --model ${CMAKE_CURRENT_SOURCE_DIR}/core/examples/simple_rankine.json
        --case design
)

add_executable(thermox_property_tests physics/tests/property_tests.cpp)
target_link_libraries(thermox_property_tests PRIVATE thermox_physics thermox_core)
if(TARGET thermox_cantera_backend)
    target_link_libraries(
        thermox_property_tests PRIVATE thermox_cantera_backend)
    target_compile_definitions(
        thermox_property_tests PRIVATE THERMOX_TEST_HAS_CANTERA=1)
endif()
add_test(NAME thermox_property_tests COMMAND thermox_property_tests)

set(THERMOX_SDK_TARGETS
    thermox_core
    thermox_physics
    thermox_platform
    thermox_service
    thermox_service_native
)
install(
    FILES $<TARGET_FILE:thermox_coolprop>
    DESTINATION ${CMAKE_INSTALL_LIBDIR}
)
if(TARGET thermox_cantera_backend)
    list(APPEND THERMOX_SDK_TARGETS thermox_cantera_backend)
endif()

install(
    TARGETS ${THERMOX_SDK_TARGETS}
    EXPORT ThermoxTargets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
install(
    DIRECTORY
        core/include/
        physics/include/
        platform/include/
        service/include/
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
    FILES_MATCHING PATTERN "*.hpp"
)
install(
    EXPORT ThermoxTargets
    FILE ThermoxTargets.cmake
    NAMESPACE thermox::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/Thermox
)

configure_package_config_file(
    cmake/ThermoxConfig.cmake.in
    ${CMAKE_CURRENT_BINARY_DIR}/ThermoxConfig.cmake
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/Thermox
)
write_basic_package_version_file(
    ${CMAKE_CURRENT_BINARY_DIR}/ThermoxConfigVersion.cmake
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion
)
install(
    FILES
        ${CMAKE_CURRENT_BINARY_DIR}/ThermoxConfig.cmake
        ${CMAKE_CURRENT_BINARY_DIR}/ThermoxConfigVersion.cmake
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/Thermox
)

set(THERMOX_SDK_STAGE_DIR
    ${CMAKE_CURRENT_BINARY_DIR}/sdk-stage)
set(THERMOX_SDK_CONFORMANCE_BUILD_DIR
    ${CMAKE_CURRENT_BINARY_DIR}/sdk-conformance)
set(THERMOX_SDK_CONFORMANCE_ENV)
if(THERMOX_ENABLE_CANTERA)
    list(APPEND THERMOX_SDK_CONFORMANCE_ENV
        PKG_CONFIG_PATH=${CANTERA_LIBDIR}/pkgconfig)
endif()
add_custom_target(
    thermox_sdk_conformance
    COMMAND
        ${CMAKE_COMMAND} -E rm -rf
        ${THERMOX_SDK_STAGE_DIR}
        ${THERMOX_SDK_CONFORMANCE_BUILD_DIR}
    COMMAND
        ${CMAKE_COMMAND} --install
        ${CMAKE_CURRENT_BINARY_DIR}
        --prefix ${THERMOX_SDK_STAGE_DIR}
    COMMAND
        ${CMAKE_COMMAND} -E env
        ${THERMOX_SDK_CONFORMANCE_ENV}
        ${CMAKE_COMMAND}
        -S ${CMAKE_CURRENT_SOURCE_DIR}/sdk/conformance
        -B ${THERMOX_SDK_CONFORMANCE_BUILD_DIR}
        -DCMAKE_PREFIX_PATH=${THERMOX_SDK_STAGE_DIR}
        -DCMAKE_BUILD_TYPE=Release
    COMMAND
        ${CMAKE_COMMAND} --build
        ${THERMOX_SDK_CONFORMANCE_BUILD_DIR}
        --parallel 1
    COMMAND
        ${THERMOX_SDK_CONFORMANCE_BUILD_DIR}/thermox_native_extension_conformance
    DEPENDS ${THERMOX_SDK_TARGETS}
    USES_TERMINAL
    COMMENT
        "Building the native extension fixture against staged Thermox SDK"
)
