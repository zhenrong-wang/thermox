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
0.121%, and the maximum absolute error is 0.282%. Every point passes the declared 0.5% cross-code
criterion.

The AGTF30 aggregate HPC extracts three bleeds. The current isolated Thermox component deliberately
does not compare outlet mass flow or shaft power because it preserves inlet flow and does not have
the three bleed extraction states needed to reproduce T-MATS bleed work. This limitation is useful
architectural evidence: the next whole-engine slice must represent the HPC as a segmented assembly
with explicit bleed ports/states instead of adding a hidden case correction to this component.
