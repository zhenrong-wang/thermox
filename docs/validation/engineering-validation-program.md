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
Thermox reproduces HPC discharge pressure and temperature with 0.121% combined MAPE and 0.282%
maximum absolute error. Outlet flow and shaft power are withheld because the AGTF30 aggregate HPC
extracts three bleeds; comparing them with a flow-preserving isolated compressor would be a false
validation.

The next T-MATS implementation is therefore the coupled shaft/combustor/turbine/nozzle system, with
the HPC represented as an explicit segmented assembly carrying its bleed extraction states. The
Sandia sCO2 transient follows once authoritative numeric response data are obtained or digitization
uncertainty is formally included in every acceptance limit.
