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
extrapolation. The next slices are the Solar Two steam-generator boundary model, a declared train/validation
split across its eight steady points, and then the NASA map import. The Sandia sCO2 transient
follows once authoritative numeric response data are obtained or digitization uncertainty is
formally included in every acceptance limit.
