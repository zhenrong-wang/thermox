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
