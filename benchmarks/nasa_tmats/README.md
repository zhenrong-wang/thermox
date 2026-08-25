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

`agtf30_high_spool.json` connects the ordinary HPC, equilibrium combustor, cooled HPT, multi-load
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

## AGTF30 coupled low-spool inverse calculation

`agtf30_low_spool.json` couples the ordinary fan, LPC, cooled LPT, a generic multi-load shaft
train, and a fixed-ratio gearbox. The topology represents a direct LPC load and a fan turning at `N2 / 3.1`; the declared
LP-shaft efficiency is 0.99 and gearbox efficiency is 1.0. The fan, LPC, and LPT maps are imported
without fitting from the arrays and component scalers in NASA's generated C model:

```sh
python3 scripts/import_tmats_c_compressor_map.py \
  /path/to/AGTF30/engine_model/MEX_engine_model.c \
  benchmarks/nasa_tmats/agtf30_fan_map.json \
  --prefix GTF_fan --id nasa-agtf30-fan-map
python3 scripts/import_tmats_c_compressor_map.py \
  /path/to/AGTF30/engine_model/MEX_engine_model.c \
  benchmarks/nasa_tmats/agtf30_lpc_map.json \
  --prefix GTF_lpc --id nasa-agtf30-lpc-map
python3 scripts/import_tmats_turbine_map.py \
  /path/to/AGTF30/engine_model/MEX_engine_model.c \
  benchmarks/nasa_tmats/agtf30_lpt_map.json \
  --prefix GTF_lpt --id nasa-agtf30-lpt-map
python3 scripts/build_agtf30_low_spool_benchmark.py
```

This deliberately remains a station-sliced inverse benchmark. Station 2 pressure/temperature and
NASA's solved fan R-line are fixed, so fan flow is reconstructed from the untouched fan map.
Station 23 flow/state, station 48 pressure/temperature, cooling state/flow, and N2 are fixed. LPT
main flow is left free and is solved from its map plus the common-shaft balance. Fixing fan R-line
is necessary because the omitted splitter, ducts, bypass/core flow paths, and nozzles provide the
downstream constraint that solves R-line in the complete engine.

Run a point with all three immutable artifacts:

```sh
./build/thermox_cli solve \
  --model benchmarks/nasa_tmats/agtf30_low_spool.json \
  --case alt_30000_mach_072_2000 \
  --performance-map benchmarks/nasa_tmats/agtf30_fan_map.json \
  --performance-map benchmarks/nasa_tmats/agtf30_lpc_map.json \
  --performance-map benchmarks/nasa_tmats/agtf30_lpt_map.json \
  --format text
```

All five points converge in two or three direct Newton iterations with normalized residuals below
`4.20e-11`; shaft power closes below `1e-6 W`. Fan flow, pressure, and temperature reconstruct to
within 0.01%, LPC discharge pressure/temperature remain within 0.20%, and solved LPT inlet flow is
within 0.01%. LPT outlet pressure differs by -2.924% to -0.539% and temperature by -1.922% to
-0.472%, while fan/LPC/LPT shaft-power differences inferred from NASA torque remain below 0.66%.
Those retained LPT state differences bound the dry N2/O2 thermochemistry and map-work differences.

This validates generic map import, a multi-load shaft plus gearbox topology, inverse LPT flow, and exact LP
shaft closure across five off-design points. It is not a continuous gas-path or whole-engine
prediction: fan R-line and intermediate station boundaries still come from NASA's solved model,
and bypass/core ducts, splitters, and nozzles are not connected in this declaration.

## AGTF30 continuous twin-spool gas-path calculation

`agtf30_continuous_twin_spool.json` removes the intermediate station sources and connects one
material graph from station 2 through the fan, core/bypass splitter, LPC, HPC, equilibrium
combustor, cooled HPT, and cooled LPT. Both shaft trains close simultaneously. Total inlet flow,
fan map coordinate, fuel flow, turbine pressure ratios, and downstream station states are solved;
NASA's measured/solved intermediate station values are used only as comparison references.

The splitter is the generic `junction.material.splitter.controlled_fraction` component. Its
case-owned signal sets the published bypass ratio while conserving every declared species and
preserving the inlet state. Regenerate the declaration with:

```sh
python3 scripts/build_agtf30_continuous_twin_spool_benchmark.py
```

Run one point with the five immutable map artifacts:

```sh
./build/thermox_cli solve \
  --model benchmarks/nasa_tmats/agtf30_continuous_twin_spool.json \
  --case alt_30000_mach_072_2000 \
  --performance-map benchmarks/nasa_tmats/agtf30_fan_map.json \
  --performance-map benchmarks/nasa_tmats/agtf30_lpc_map.json \
  --performance-map benchmarks/nasa_tmats/agtf30_hpc_map.json \
  --performance-map benchmarks/nasa_tmats/agtf30_hpt_map.json \
  --performance-map benchmarks/nasa_tmats/agtf30_lpt_map.json \
  --residual-tolerance 1e-9 \
  --format text
```

All four zero-VBV points converge in five Newton iterations with residual norms from `1.17e-11`
to `1.64e-10`; both shaft balances close below `1e-6 W`. Relative to NASA T-MATS, solved inlet
flow differs by +1.264% to +1.634%, fan pressure by -0.473% to -0.332%, LPC pressure by -2.027%
to -1.042%, HPT outlet flow by +0.930% to +1.316%, and LPT outlet pressure by +2.999% to +4.521%.
The equilibrium calculation solves fuel 15.656% to 16.088% below T-MATS and station-4 temperature
2.083% to 2.258% low. Those differences are retained rather than calibrated away.

This is the first **continuous turbomachinery gas-path cross-code inverse reproduction**. It
validates simultaneous mass/species/energy/map/two-shaft closure without intermediate station
boundaries. It is not a whole-engine or thrust prediction: the current declaration omits the
source model's Mach-dependent duct losses, variable bleed valve relation, and core/bypass nozzles.
The sea-level-static point is excluded because its nonzero VBV command cannot be represented
honestly until that generic component is present. The explicit `1e-9` solve tolerance is local to
this 266-variable benchmark; the platform default remains `1e-10`.

## AGTF30 ducted twin-spool calculation

`agtf30_ducted_twin_spool.json` extends the continuous graph with five ordinary
`transport.material.perfect_gas_mach_scaled_loss` instances. Each duct solves its local subsonic
Mach number from total state, species flow, physical area, and declared heat-capacity ratio, then
applies the T-MATS quadratic design-Mach pressure-loss law. Areas and loss coefficients are copied
from the public generated engine model; none are fitted to its outputs.

```sh
python3 scripts/build_agtf30_ducted_twin_spool_benchmark.py
```

All four zero-VBV points converge in five Newton iterations with residual norms below `4.12e-10`.
Adding the declared ducts reduces inlet-flow error to +0.075% through +0.412%, fan-pressure error
to -0.083% through -0.026%, LPC-pressure error to -0.395% through -0.274%, and HPT-flow error to
-0.243% through +0.119%. LPT outlet pressure differs by -3.397% through -0.187%. The retained fuel
difference is -16.426% through -15.769%, confirming that duct loss was not its cause.

This remains short of whole-engine validation. Bypass ratio and HP speed are still fixed from the
reference solution, while the VBV and both convergent nozzles are absent. The next gate adds nozzle
flow-capacity/back-pressure closure, which will allow bypass ratio and HP speed to become solved
variables rather than reference boundaries.

## AGTF30 nozzle-closed zero-VBV engine calculation

`agtf30_nozzle_closed_twin_spool.json` replaces both open sinks with generic perfect-gas
convergent nozzles. Each nozzle solves choked/un-choked Mach, capacity, and gross thrust from its
material total state, a dimensionless area command, and a dimensionless ambient-pressure command.
Typed reference area and pressure parameters preserve dimensional correctness. The bypass command
uses NASA's scheduled VAFN area; the core throat area and both gross-thrust coefficients come
directly from the generated source model.

The nozzle constraints allow the splitter fraction and HP shaft speed to be released. Across four
zero-VBV points, both are solved rather than copied from T-MATS. The graph converges in five to nine
Newton iterations with residual norms below `2.04e-11`. HP-speed error is +0.026% to +0.109%,
bypass-ratio error is -0.613% to -0.482%, and combined core-plus-bypass gross-thrust error is
approximately +0.14% to +0.25%.

Generate the final declaration after producing the four ducted steady results in `tmp/`:

```sh
python3 scripts/build_agtf30_nozzle_closed_twin_spool_benchmark.py \
  --warm-start-directory tmp
```

Warm-start results condition only initial guesses; the nozzle equations, component parameters,
boundaries, and converged acceptance outputs do not depend on them. This qualifies a complete
map-driven **zero-VBV steady gas path** against the public cross-code reference. Net thrust remains
reserved until the generic ambient/inlet ram-drag path is connected. The fifth sea-level-static
point and the published transient remain reserved until the generic VBV is present.

## AGTF30 map-driven variable-bleed-valve calculation

`agtf30_vbv_nozzle_twin_spool.json` inserts a generic
`junction.material.cross_bleed.performance_map` between the LPC discharge and bypass stream. The
component evaluates a bound corrected-flow characteristic by donor/receiver pressure ratio and
dimensionless position, removes the resulting flow with the donor composition, and conservatively
mixes its species and enthalpy into the receiver. The reusable component contains no AGTF30 or
T-MATS branch; `agtf30_vbv_map.json` is the immutable NASA instance data.

```sh
python3 scripts/build_agtf30_vbv_nozzle_twin_spool_benchmark.py
```

The four assembled whole-engine points command the valve closed. They therefore form a regression
gate proving that inserting the physical routing component does not perturb the nozzle-closed
solution: the four points converge in five to nine Newton iterations and each solves exactly zero
bleed.

`agtf30_vbv_open_point.json` independently qualifies the remaining sea-level-static open-valve
physics at the station states produced by the five-point low-spool benchmark. With position 0.37,
donor pressure 20.887087 psia, bypass pressure 15.154323 psia, and donor temperature 582.670196
degR, NASA's `Valve_TMATS_body.c` relation gives 4.633915 lbm/s (2.101908 kg/s). The generic
Thermox map-bound cross-bleed component reproduces that flow to numerical precision and closes
both donor removal and receiver addition exactly. This is a staged component/subsystem
qualification; the fully coupled fifth static engine point remains reserved while its nozzle/VBV
closure is conditioned. Net thrust remains a separate gate because ram drag is not yet represented
in the graph.

`agtf30_vbv_bypass_branch.json` then relaxes the bypass pressure and closes it through the source
duct-17 loss model and scheduled 8000 in2 convergent nozzle. This three-component predictive
subsystem converges in three trust-region Newton iterations. It reconstructs the NASA station-13
pressure within 0.0043 percent and the independently qualified VBV flow within 0.0037 percent,
while producing a finite positive gross thrust. No pressure or flow correction is fitted. This
confirms that the static bypass branch is compatible; the remaining fifth-point blocker lies in
the simultaneous core/LP-shaft/whole-engine coupling.

`agtf30_core_exhaust_branch.json` performs the symmetric terminal test from the public static
LPT-exit temperature, composition basis, and flow. It relaxes station-5 pressure and closes the
generic duct-5 loss model against the fixed 393.43 in2 core nozzle and sea-level ambient pressure.
The branch converges in three trust-region Newton iterations and predicts 103.192 kPa versus the
NASA station-5 value of 103.199 kPa (-0.0063 percent). The
boundary uses the same N2/O2 thermochemistry translation as the qualified low-spool slice, so this
gate qualifies terminal capacity and pressure closure, not combustion-product composition.

## AGTF30 sea-level-static open-VBV whole-engine calculation

`agtf30_vbv_nozzle_static.json` closes the previously reserved fifth point with the complete
407-variable graph: five coordinate maps, equilibrium combustor, declared cooling extractions,
open map-bound VBV, five ducts, two nozzles, geared LP shaft, and extracted-power HP shaft. A
capacity-matched numerical homotopy starts from the independently qualified terminal flows and
continues the two nozzle areas to NASA's scheduled 8000 in2 bypass area and 393.43 in2 core area.
Only initial guesses vary along this path; component equations, target areas, maps, boundaries, and
acceptance outputs are unchanged.

The point reaches the target in 12 accepted continuation stages with a normalized residual of
2.16e-13. Relative to the public NASA solver it gives fan flow -0.033 percent, HPT pressure -0.111
percent, HPT temperature -0.991 percent, HPT flow +0.372 percent, station-5 pressure +0.012
percent, station-5 temperature -1.014 percent, VBV flow -0.369 percent, and bypass ratio -0.468
percent. Both shaft balances close to numerical precision. Fuel remains -15.24 percent, consistent
with the previously declared combustion/fuel-model prediction limitation; no calibration is
applied. Gross thrust is solved, but comparison and net thrust remain reserved until the public
source output extraction and generic inlet ram-drag path are both qualified.

## AGTF30 transient source baseline

The pinned AGTF30 source also establishes the next validation target without yet claiming a
Thermox transient reproduction. NASA linearizes a quasi-steady gas path around two differential
states—LP and HP shaft speed—and three inputs: fuel flow, HP shaft extraction, and LP shaft
extraction. For the sea-level-static case, the public `outputs.mat` contains:

```text
X = [3100.00004321, 17750.92161390] rpm
U = [0.30826616 lbm/s, -350 hp, 0 hp]
A = [[-2.58828958,  1.11503138],
     [ 0.78936733, -1.88527248]]
B = [[4405.37064, 0,          0.547524024],
     [8570.82043, 5.13251170, 0          ]]
```

NASA declares LP and HP inertias of 17.44087229 and 1.86055038 slug-ft2, respectively
(23.64664769 and 2.52256760 kg-m2). Thermox now has generic instance-sized multi-load inertial
shaft components plus composable gearboxes capable of representing those two rotor states. Its explicit
quasi-steady algebraic contract now lifts the map-driven gas path into the transient DAE without
duplicating component physics.

The reproducible `agtf30_vbv_nozzle_transient.json` declaration derives from the converged Thermox
static state, replaces only the two shaft trains with their generic inertial counterparts, fixes
fuel instead of LP speed, and retains the six public performance-map artifacts. The resulting
419-variable DAE has two differential rotor-energy states. It reaches 0.001 s in one accepted step,
with no rejection and a maximum absolute normalized residual of 5.58e-10.

Thermox's generic index-1 DAE tangent linearizer releases fuel flow plus independent HP and LP
extractions as exogenous inputs, forms the scaled local response Jacobian, factors it once, and
solves five sensitivity right-hand sides. In native rotor-energy coordinates it obtains:

```text
A_E = [[-2.55830,  1.82347],
       [ 0.370274, -1.92434]]
B_E = [[ 9.24284e6,  0, -1],
       [ 1.15416e7, -1,  0]]
```

After the exact local coordinate change from rotor energy to rpm, the state matrix is
`[[-2.55830, 1.11491], [0.605595, -1.92434]]`. Relative to NASA, the LP diagonal, LP-from-HP
coupling, and HP diagonal differ by 1.16%, 0.011%, and 2.07%; HP-from-LP coupling differs by
23.3%. Fuel gains converted to rpm/s per lbm/s differ by 18.4% and 24.3%. Extraction topology and
sign are now reproduced independently on both rotors, but the gains remain materially different:
HP is 1.52 versus NASA's 5.13 rpm/s per signed hp, while LP is 0.928 versus 0.548. This is therefore
a complete-input but partial dynamic cross-code reproduction and a qualification of the generic
DAE linearization path, not full transient predictive validation. Remaining differences are
retained as evidence rather than calibrated away.
