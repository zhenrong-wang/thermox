# NASA T-MATS gas-turbine cross-code validation

This benchmark begins Thermox's map-rich gas-turbine validation with the smallest auditable
slice: the high-pressure compressor in the public T-MATS simple-turbojet example. NASA's
`NASA/TM-2014-218410` Table 4 publishes the design-point station values and compressor torque for
both T-MATS and NPSS. The T-MATS repository provides the exact compressor map and scalers.

## Reproducible inputs

`hpc_map.json` is generated from the official `setup_HPC.m` by
`scripts/import_tmats_compressor_map.py`. The importer preserves R-line as the map's primary
coordinate. This matters near choke: corrected flow repeats at several R-line values while
pressure ratio changes, so treating corrected flow as the independent coordinate would merge
physically distinct points.

The generated artifact records both the source-file SHA-256 and canonical payload SHA-256. To
regenerate it from a checkout of NASA T-MATS at the pinned commit:

```sh
python3 scripts/import_tmats_compressor_map.py \
  /path/to/T-MATS/Trunk/TMATS_Examples/Example_GasTurbine_SS/SimSetup/setup_HPC.m \
  benchmarks/nasa_tmats/hpc_map.json
```

## Execute

The model and immutable map declaration are separate, ordinary platform inputs:

```sh
./build/thermox_cli solve \
  --model benchmarks/nasa_tmats/hpc_design_point.json \
  --case published_design_point \
  --performance-map benchmarks/nasa_tmats/hpc_map.json \
  --format text
```

Thermox uses the source map's flow, pressure-ratio, efficiency, and corrected-speed scalers without
fitting. At the published inlet state, mass flow, and shaft speed, it solves R-line from corrected
flow and predicts discharge pressure, discharge temperature, and shaft power. Cantera supplies the
air thermochemistry; the declared 0.768 N2 / 0.232 O2 mass split is an explicit approximation.

Pinned results versus the T-MATS column in Table 4 are +0.091% discharge pressure, -0.265%
discharge temperature, and +0.290% shaft power inferred from published torque. All pass the 0.5%
cross-code band stated in the NASA report. The normalized nonlinear residual is `1.15e-11`, and
component mass and energy metrics close to their reported numerical zeros.

This is a **map-import and design-point cross-code reproduction**, not independent experimental
validation. It proves the new coordinate-map abstraction can represent a real public compressor
map without lossy inversion and that the resulting component calculation agrees with NASA's code
comparison. It does not yet prove off-design, whole-engine, or transient predictive capability.
All exact source locations, assumptions, and results are pinned in `source_manifest.json`.

## AGTF30 multi-point off-design slice

NASA's newer public AGTF30 MEX repository includes numeric inputs and outputs for seven solved
steady operating conditions. `agtf30_hpc_off_design.json` uses the five rows that have nominal
health parameters and no sensor/actuator biases. They span sea level through 30,000 ft, Mach 0
through 0.72, and requested corrected fan speeds from 1,000 to 2,000 rpm. The degraded-health and
sensor-bias demonstration rows are excluded rather than silently treated as nominal physics.

The AGTF30 model uses the same public 143-point raw HPC map with different engine-specific scalers.
`agtf30_hpc_map.json` records those scalers and both source identities. Regenerate it with:

```sh
python3 scripts/import_tmats_compressor_map.py \
  /path/to/T-MATS/Trunk/TMATS_Examples/Example_GasTurbine_SS/SimSetup/setup_HPC.m \
  benchmarks/nasa_tmats/agtf30_hpc_map.json \
  --id nasa-agtf30-hpc-map --revision agtf30-17051fc \
  --speed-scale-rpm 18242.834381 --flow-scale 0.1328 \
  --pressure-ratio-scale 0.595594 --efficiency-scale 0.994014
```

The override values are copied from the pinned AGTF30 generated C model and are recorded beside the
original simple-turbojet scalers in the import audit. Execute one point with the same declaration
path:

```sh
./build/thermox_cli solve \
  --model benchmarks/nasa_tmats/agtf30_hpc_off_design.json \
  --case alt_30000_mach_072_2000 \
  --performance-map benchmarks/nasa_tmats/agtf30_hpc_map.json \
  --format text
```

No parameters are fitted to the five outputs. Across all five points, discharge-pressure errors
are -0.004% to -0.010%, discharge-temperature errors are -0.198% to -0.282%, combined MAPE is
0.121%. A generic instance-sized fractional-bleed port group represents the three source-declared
extractions and their pressure/enthalpy state fractions. Outlet-flow error is numerical zero and
torque errors are +0.198% to +0.283%; the four-output MAPE is 0.122% and the maximum absolute error
is 0.283%. Every point passes the declared 0.5% cross-code criterion.

The bleed count and state fractions are ordinary component-instance declarations, not an AGTF30
special case. The same model can expose one through 32 extraction ports, and its shaft balance
accounts for work retained by each extracted stream. This closes the isolated HPC flow/work gap;
the next whole-engine slice is the coupled shaft, combustor, turbine, and nozzle system.

## AGTF30 high-pressure-turbine inverse-map slice

T-MATS turbine maps use pressure ratio as their primary coordinate and publish corrected flow as
an output. `turbine.material.coordinate_map` preserves that orientation: Thermox solves pressure
ratio from flow compatibility rather than numerically inverting or resampling the source map.
`scripts/import_tmats_turbine_map.py` imports the generated C arrays, applies the four declared
component scalers and converts corrected quantities to the platform's reference-state SI basis.
The importer also preserves T-MATS' pressure-major C-array layout explicitly.

Across the same five healthy operating points, the solved HPT pressure-ratio and discharge-pressure
errors remain below `0.000057%`. The generic instance-sized cooling group places one compressor
bleed at the stage inlet and one at the turbine exit, matching the declared T-MATS positions. Outlet
flow closes at numerical precision and torque errors range from -0.110% to +0.077%.

This is a **cooled-turbine component cross-code reproduction**, but not yet the coupled high-spool
result: NASA's station-4 boundary remains fixed and the slice uses a simplified N2/O2 working-gas
composition. That explicit approximation produces outlet-temperature errors of -0.685% to -1.283%,
so temperature remains reserved until the combustor supplies a consistent product composition.

## AGTF30 coupled high-spool inverse calculation

`agtf30_high_spool.json` connects the ordinary HPC, equilibrium combustor, cooled HPT, two-load
shaft train, and boundary components. HPC bleed 2 enters at the turbine exit, bleed 3 enters at the
stage inlet, and the declared `-350 hp` NASA HP-power command is represented as a positive 350 hp
shaft load. Station 25 and shaft speed are fixed upstream boundaries. Fuel flow is deliberately
left free and is solved from the common-shaft balance; it is not copied from NASA's command vector.

The methane/air calculation uses `agtf30_major_products.yaml`, a seven-species ideal-gas equilibrium
phase whose thermodynamic data are imported unchanged from Cantera's GRI-Mech 3.0 declaration. The
reduced basis avoids carrying 46 immaterial trace-species flow unknowns through compressor and
turbine ports. This is a visible model declaration, not a solver specialization. Regenerate the
model deterministically with:

```sh
python3 scripts/build_agtf30_high_spool_benchmark.py
```

Run a coupled point with both immutable map inputs:

```sh
./build/thermox_cli solve \
  --model benchmarks/nasa_tmats/agtf30_high_spool.json \
  --case sea_level_static_1000 \
  --performance-map benchmarks/nasa_tmats/agtf30_hpc_map.json \
  --performance-map benchmarks/nasa_tmats/agtf30_hpt_map.json \
  --format text
```

All five points converge in three direct Newton iterations with normalized residuals from
`3.15e-12` to `1.98e-11`. HP-shaft power closes below `1e-6 W`. Against T-MATS, station-4
temperature differs by +0.364% to +0.524%, HPT outlet flow by -0.232% to -0.160%, outlet pressure
by +2.501% to +3.612%, and outlet temperature by +0.996% to +1.319%.

The inverse fuel result is 12.01% to 12.77% below the T-MATS command. That discrepancy is retained,
not calibrated away. It bounds the combined difference between T-MATS' station-level burner/FAR
thermodynamics and the declared equilibrium chemistry plus Thermox map-work calculation. Therefore
this case validates generic graph coupling, bleed routing, inverse-map compatibility, thermochemical
state propagation, and exact shaft closure. It does **not** yet qualify fuel-flow prediction or
constitute independent hardware validation. The 0.84 multiplier in the generator affects only the
initial fuel guess; fuel remains an unconstrained solved variable and the converged result is
independent of that initialization within the solver's basin.
