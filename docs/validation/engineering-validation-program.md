# Thermox engineering validation program

Thermox is not yet entitled to an unrestricted “engineering ready” claim. It has strong numerical
regressions and useful public design-point reproduction, but the remaining claim requires
independent held-out system outputs, map-driven off-design behavior, and a measured transient.

The selected cases are intentionally complementary rather than numerous:

- **NETL B31A** remains the complex combined-cycle design reference. It exercises a connected
  triple-pressure HRSG and steam train, detailed steam states, and plant balances, while retaining
  its missing OEM maps and off-design data as declared limits.
- **Solar Two (SAND2002-0120)** is the independent utility-scale experimental case. Its eight
  steady steam-generator/turbine-generator tests, storage heat losses, startup energy, and reported
  uncertainties anchor physical evidence. Missing detailed raw states prevent overclaiming a fully
  specified first-principles cycle today.
- **NASA T-MATS twin-spool examples and NASA/TM-2014-218410** provide Apache-licensed,
  non-proprietary maps and reproducible steady/transient reference behavior. This is the strongest
  public test of map interpolation, bleeds/cooling, multi-shaft closure, nozzles, controls, and fuel
  steps, but it remains cross-code evidence rather than independent hardware evidence.
- **Korea University 1 kW R245fa ORC** provides 77 complete steady hardware points with a clean
  distinction between ten external inputs and internal system responses. It is the primary next
  whole-system held-out physical gate. The source is CC BY 4.0; the associated papers describe
  charge-sensitive pressure formation and passive receiver behavior that must be modeled rather
  than replaced with measured internal pressures.
- **Sandia sCO2 loop** provides near-critical real-fluid hardware and transient behavior. The
  publications cite the `GenIV_101201_1041.csv` source file but do not publish it for download;
  plotted series can support an uncertainty-qualified digitized study, not metrology-grade raw-data
  validation.

The machine-readable selection, limits, and promotion gates live in
`benchmarks/validation_portfolio.json`. Each case also owns a source manifest that records official
URLs, retrieval date, checksums, exact extracted locations, and interpretation limits. Source PDFs
are never required in the repository and are not committed.

## Claim ladder

“Numerically credible” requires conservation and scaled residual limits, solver-policy
independence, and steady/transient regressions. “Physically credible for the declared scope” adds
independent property/component criteria, uncertainty-aware acceptance, and explicit calibration
and assumption classification. “Engineering ready” additionally requires:

1. one independent physical system case with held-out output agreement;
2. one map-driven off-design case;
3. one measured transient case;
4. no required result that depends on a hidden case-specific correction.

The evidence service remains the authority for classification. Numerical closure cannot be
promoted into physical validation, calibrated reproduction cannot be presented as independent
prediction, and a digitized chart cannot be represented as a raw instrument series.

The first ORC accounting slice is complete across all 77 measured points. Thermox reconstructs
R245fa states through its provider-open CoolProp HEOS package and obtains a closed-loop energy
residual of 0.763% mean absolute and 1.552% maximum absolute, within the preregistered 1%/2%
accounting gate. This validates property reconstruction and conservation against hardware data; it
is not external-boundary prediction because measured internal pressures and temperatures are used.
The platform now also has a generic inventory connector, property-backed steady rigid-volume
holdup, an instance-sized fixed-total-charge constraint, and inventory exposure from constant-mass
exchanger cells plus variable-mass two-phase cells, correlated volumes, and drums. The next ORC gate must supply or
identify component volumes and freeze pump, expander, heat-exchanger, and receiver model artifacts
on cases 1--68 before cases 69--77 are opened.

## First implementation sequence

The Solar Two nitrate-salt loop exposed a general property gap. Thermox now has both a
CoolProp-backed incompressible-fluid property family and a generic tabulated incompressible
provider. The latter integrates piecewise-linear heat capacity to give mutually consistent PT/PH/PS
states. Sandia's published 60 mass-% NaNO3 / 40 mass-% KNO3 table is its first registered dataset.
These are reusable platform capabilities for solar, thermal storage, industrial heat-transfer
loops, and future user-registered incompressible fluids; neither is embedded in a Solar Two
component.

CoolProp retains its declared 300°C NaK lower limit. The Solar Two model instead uses
SAND2001-2100 Table 1-1, whose 500--1100°F range covers the plant's 290°C cold state without
extrapolation.

The declared Solar Two train/validation split is now executable. A generic conservative
fluid-to-electric reduced-order component is calibrated only on Appendix M tests 1--4 and frozen
before tests 5--8. The held-out gross-power MAPE is 5.12% and the maximum absolute relative error
is 9.16%. This advances the independent-system gate, but it does not complete it: missing
feedwater, steam-flow, salt-outlet, condenser, and turbine-exhaust boundaries mean the result is a
calibrated system response rather than first-principles steam-cycle validation.

The first NASA T-MATS slice is now complete. A deterministic importer preserves the public HPC
map's R-line coordinate, all 143 map points, source scalers, and source checksum. The generic
`compressor.material.coordinate_map` component solves the map coordinate from corrected-flow
closure. At the NASA Table 4 design point, Thermox differs from T-MATS by +0.091% in compressor
discharge pressure, -0.265% in temperature, and +0.290% in shaft power inferred from published
torque. All three pass the report's 0.5% cross-code comparison band. This completes map-import and
design-component reproduction; whole-engine, transient, and hardware gates remain open.

The off-design component gate is now also complete. Five healthy, unbiased operating points from
NASA's public AGTF30 numeric output span sea level to 30,000 ft and Mach 0 to 0.72. Without fitting,
Thermox reproduces HPC discharge pressure, temperature, outlet flow, and torque with 0.122%
combined MAPE and 0.283% maximum absolute error. A generic instance-sized fractional-bleed port
group carries the three source-declared extraction states and their work contribution; no
case-specific correction is fitted.

The coupled dynamic shaft/combustor/turbine/nozzle implementation now completes both open-loop and
closed-loop 10.5 s replays against NASA's full nonlinear 701-point reference. Exact-boundary slices
first exposed and corrected generic declared-LHV and convergent-nozzle pressure-thrust semantics.
The aligned open-loop plant reaches every timestamp with a `9.55e-9` maximum normalized residual;
shaft-speed MAPE is 0.025% and predictive-signal mean MAPE is 0.505%. The closed-loop graph then
replaces forced fuel with a shaft-speed pickup, 0.05 s sensor, published PI controller, and
commanded fuel source. It reaches every timestamp with a `9.08e-9` residual; shaft-speed MAPE is
0.00875%, predicted fuel-flow MAPE is 0.443%, net-thrust MAPE is 0.788%, and predictive plant-signal
mean MAPE is 0.502%. This closes the public nonlinear cross-code transient gate for this model.
It does not close hardware qualification: the remaining FAR-table/equilibrium-property difference,
published controller assumptions, and absence of measured uncertainty remain explicit limits.
The Sandia sCO2 transient follows once authoritative numeric response data are obtained or
digitization uncertainty is formally included in every acceptance limit.

The first turbine prerequisite is complete. A generic coordinate-map material turbine consumes
the native pressure-ratio/corrected-speed map orientation and solves pressure ratio through
corrected-flow compatibility. Against five AGTF30 HPT points, pressure-ratio and discharge-pressure
errors are below 0.000057%. Generic front-stage/rear-exit cooling injections close outlet flow at
numerical precision and reproduce torque within 0.110%. The simplified N2/O2 station-4 boundary
leaves outlet temperature 0.685--1.283% low; combustor composition and coupled shaft balance remain
open system gates.

The LP-spool coupling gate is now complete as a station-sliced cross-code reproduction. Generic
fan, LPC, cooled LPT, multi-load shaft, and gearbox components solve all five healthy AGTF30 points in
two or three Newton iterations and close shaft power below `1e-6 W`. Fan map outputs and inverse LPT
flow agree within 0.01%; LPC discharge temperature remains within 0.123%, while the simplified
dry-air LPT outlet retains pressure and temperature differences up to 2.924% and 1.922%. NASA's
solved fan R-line is a declared boundary because the downstream splitter, dual gas paths, ducts,
and nozzles are not yet connected. The next gate is therefore continuous whole-engine gas-path
closure, not additional fitting of this sliced benchmark.

The continuous turbomachinery gas-path gate is now complete. A generic signal-controlled species
splitter connects the fan to the bypass branch and the LPC/HPC/combustor/HPT/LPT core path; both
shaft trains close in the same 266-variable solve. Four zero-VBV operating points converge in five
Newton iterations. Inlet-flow error is at most 1.634%, HPT outlet-flow error 1.316%, and LPT outlet
pressure error 4.521% against T-MATS. The uncalibrated equilibrium fuel result remains 15.656--
16.088% low, so fuel prediction remains explicitly reserved.

This advanced the program from sliced component/spool checks to a continuous map-driven engine
core. Subsequent generic duct, variable-bleed, nozzle, freestream-momentum, and force-balance gates
now complete the steady flight-point topology. The previously missing nonlinear reference is now
exported from NASA's unmodified simple-gas-turbine Simulink example and exercised by aligned
open-loop and closed-loop Thermox replays. Model-form differences are quantified by exact-boundary
component slices; no discrepancy is hidden with a case-specific factor.

The first whole-engine prerequisite is also complete: five generic area-based gas ducts reproduce
the source model's local-Mach quadratic loss law. With no output fitting, inlet-flow error falls
below 0.42%, LPC pressure below 0.40%, and HPT flow below 0.25% across the four zero-VBV points.
The graph now has 361 simultaneous variables. The remaining steady whole-engine gates are the
generic convergent nozzles and VBV; only after their capacity constraints replace the current
reference bypass-ratio and HP-speed boundaries can net thrust be classified.

The nozzle gate is now complete for the four zero-VBV points. Generic convergent material nozzles
close the core and bypass paths, releasing bypass ratio and HP speed as solved quantities. Their
maximum cross-code errors are 0.613% and 0.109%, respectively, while combined gross thrust remains
within approximately 0.25%. This is the first complete steady AGTF30 gas-path reproduction, but it
is still cross-code rather than hardware evidence. The generic ambient/inlet ram-drag gate is
reported below.

The generic VBV gate is now implemented as an artifact-driven controlled material cross-bleed
junction. It uses pressure ratio and position to evaluate corrected bleed capacity, preserves the
donor composition, and conserves species and enthalpy while mixing into the receiving stream. The
NASA flow curve is bound as ordinary instance data. Inserting the component into the complete
nozzle-closed graph leaves all four closed-valve points convergent and produces exactly zero bleed.
The fifth open-VBV point still needs to be assembled from the public source boundaries. The four
nonzero-Mach net-thrust points are closed by the later generic freestream/ram-drag gate.

Attempting that large sea-level-static move exposed a separate generic numerical gap: component
homotopy existed, but fixed ambient, shaft-speed, nozzle-area, and actuator boundaries remained at
their final values. Fixed boundaries can now opt into an anchor-to-target physical path through the
case's `boundary_continuation` solver option, and the numerical core can recognize such a complete
parameterized path without applying a second diagonal residual blend. Configurable CLI step bounds
make the behavior reproducible. The fifth AGTF30 endpoint is still reserved: even with very small
steps, the current line-search Newton globalization can leave Cantera's admissible enthalpy domain.
The next numerical gate is a bound-aware trust-region or filter globalization, not a case-specific
relaxation of thermochemistry.

That numerical gate and the fifth steady point are now complete. Bound-aware trust-region Newton
globalization plus capacity continuation closes the 407-variable sea-level-static graph in 12
accepted stages at a normalized residual of `2.16e-13`. The public cross-code differences are
-0.033% fan flow, -0.111% HPT pressure, -0.991% HPT temperature, +0.372% HPT flow, +0.012%
station-5 pressure, -1.014% station-5 temperature, -0.369% VBV flow, and -0.468% bypass ratio.
Fuel remains -15.24% and is explicitly unqualified; the difference is not calibrated away.

The previously reserved propulsion outputs have now been extracted directly from NASA's pinned
`outputs.mat`. A checksum-guarded artifact preserves the published ram drag, combined gross
thrust, net thrust, bypass/core gross thrust, and TSFC fields for five healthy points. Thermox's
already solved combined gross thrust differs by +0.1408% to +0.3261% across those points. A generic
freestream-momentum component and instance-sized propulsive force balance now close all four
nonzero-Mach points: ram-drag error is -0.123% to -0.057%, and net-thrust error is +0.844% to
+1.258%. Freestream speed comes from published Mach and static temperature through the declared
ideal-air relation; source forces are acceptance targets and initial guesses only. This qualifies
steady net thrust within the public cross-code scope, not against independent hardware evidence.

The first external-reference trajectory gate is now executable as well. A one-percent scheduled
fuel step drives the complete 419-variable nonlinear Thermox DAE, while the checksum-pinned NASA
A/B model supplies the same absolute-input rotor-speed reference at five times through 0.050 s.
The integration accepts six steps without rejection and closes normalized residuals below
`5.44e-9`. Thermox's LP-speed increment is 17.81%--18.12% above NASA and its HP-speed increment is
21.64%--21.89% above NASA. These differences are consistent with the separately identified local
fuel-gain mismatch. This promotes the work beyond internal trajectory self-consistency, but it
does not qualify nonlinear or hardware transient prediction because the public source provides no
exported numeric nonlinear time history.

Thermox now also has an executable held-out gate for multi-operating-point local-model families.
Validation cases are excluded from interpolation construction, independently initialized and
linearized, and then compared entry-by-entry against the training-only A/B/C/D prediction under
declared absolute-plus-relative tolerances. The analytical storage gate passes and a deliberately
mislocated validation coordinate fails. This strengthens the validation machinery, but it is
internal-consistency evidence. The next engineering-evidence use is to promote traceable AGTF30
dynamic operating points into training and held-out cases; measured transient evidence remains
required for the unrestricted transient claim.
