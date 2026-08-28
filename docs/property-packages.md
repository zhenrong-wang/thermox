# Property Packages

## Reacting mixtures

Fixed-fluid property packages and reacting-mixture thermochemistry are separate Thermox contracts.
`ThermochemistryPackage` owns:

- an ordered species basis and validated mass- or mole-fraction compositions;
- composition-aware PT and PH states;
- composition-aware PS states for isentropic compression and expansion;
- equilibrium at fixed enthalpy and pressure;
- reference-state lower heating value from complete stoichiometric combustion with water vapor;
- mechanism, phase, implementation, version, and capability identity.

`ThermochemistryPackageRegistry` constructs these packages by backend, mechanism, and phase.
Combustors and reacting-material components depend on this interface rather than directly on
Cantera or another chemistry library. This preserves backend replacement and service catalog
discovery.

The built-in `combustor.material.adiabatic_equilibrium` component accepts independent air and fuel
material streams on the same complete mechanism species basis. It mixes their species mass flows
and enthalpy, applies a configured pressure ratio, calls equilibrium at constant enthalpy and
pressure, and closes every outlet species mass flow from the returned equilibrium composition.
The model contains no fuel name, stoichiometric formula, or gas-turbine topology. A small
per-solve cache ensures all species residual rows share one equilibrium evaluation at a given
Newton iterate.

For performance-test and reduced-order equipment models, the separate
`combustor.material.equilibrium_heat_release_efficiency` component additionally requires a
declared `combustion_efficiency` and `fuel_lower_heating_value`. It removes
`(1 - combustion_efficiency) * m_fuel * LHV` from the reacting-stream enthalpy balance before the
same equilibrium calculation. The resulting energy difference is visible as a component and
system loss. This is an explicit heat-release-efficiency approximation; it does not claim to
predict unburned-species composition or detailed flame kinetics.

Upstream gaseous-fuel pressure can be represented without forcing it to equal the combustor
network pressure through `regulator.material.isenthalpic_network_pressure`. The regulator
conserves species flow and specific enthalpy while allowing its connected downstream system to
determine outlet pressure. It is a reduced-order throttling boundary; it does not predict valve
capacity, actuator position, or pressure-control dynamics.

Cantera 3.2.0 is pinned under `modules/properties/cantera`. The adapter is intentionally optional:
install the pinned Cantera C++ library into an isolated prefix, make its `cantera.pc` visible to
`pkg-config`, and configure Thermox with:

```bash
cmake -S . -B build-cantera -DTHERMOX_ENABLE_CANTERA=ON
```

This produces `thermox::cantera_backend`, whose registration function adds composition-aware PT,
PH, PS, equilibrium-HP, transport, and lower-heating-value capabilities. The default build remains
bounded and does not compile Cantera. When the option is enabled, the native service runtime links
and registers the Cantera backend automatically; callers can still construct an explicit runtime
registry for dependency injection and testing.

The lower-heating-value operation accepts a backend-neutral fuel composition and an explicit
reference pressure and temperature. The Cantera adapter forms a stoichiometric pure-oxygen
mixture, closes complete C/H/N combustion to CO2, water vapor, and N2 at the same reference state,
and reports heat release per unit mass of the original fuel composition. It is intended for
fuel-basis consistency and performance-test reduction. It does not infer an OEM fuel correction,
substitute for a calorimeter value, or silently convert between LHV and HHV. Compositions requiring
other complete-combustion products must use a backend that explicitly supports them.

The built-in `compressor.material.isentropic_efficiency` and
`turbine.material.isentropic_efficiency` components preserve each species mass flow, evaluate the
isentropic outlet through the PS contract, apply isentropic efficiency, and close shaft power.
They use the same component graph semantics as fixed-fluid turbomachinery and contain no
gas-turbine-specific assumptions.

`core/examples/brayton_cantera.json` is the public reacting-cycle integration benchmark. It
assembles generic registry components into a methane-air Brayton graph and checks independently
reproducible Cantera reference values: approximately 34.80 MW compressor power, 69.86 MW turbine
power, 33.85 MW net electric power, 1418.70 K turbine inlet, and 864.30 K exhaust. The benchmark
is a platform graph and regression test, not a hard-coded gas-turbine solver.

`humid_air_state_ptrh` is an always-available CoolProp-backed ambient service. It converts measured
pressure, dry-bulb temperature, and relative humidity into humidity ratio, water mass fraction,
and thermodynamic/transport properties on a humid-air mass basis. Its humidity ratio is intended
to initialize a conserved H₂O composition for the higher-temperature thermochemistry path; the
humid-air correlations are not used outside their documented range.

Thermox separates numerical algorithms from thermophysical models:

```text
thermal components and systems
            |
     thermox_physics
  PropertyPackage (SI units)
            |
  +---------+---------+
  |         |         |
ideal gas  CO2      water/steam
            \        /
             CoolProp
            |
     thermox_core
 steady Newton / transient DAE
```

`thermox_core` has no dependency on a fluid model. The physics layer converts
real fluid states into a uniform `ThermodynamicState` and maps backend failures
to explicit `PropertyStatus` values. Component equations can therefore switch
property packages without changing solver contracts.

## Current contract

The public C++ interface is
`physics/include/thermox/physics/property_package.hpp`. It provides:

- pressure-temperature (`state_pt`), pressure-enthalpy (`state_ph`), and
  pressure-entropy (`state_ps`) flashes;
- a backend-neutral `state_ph_derivatives` result containing the state and
  temperature, density, internal-energy, entropy, vapor-quality, constant-pressure and
  constant-volume heat-capacity, and speed-of-sound partials with respect to pressure and
  enthalpy;
- explicit capability discovery for PT, PH, PS, and transport operations, allowing the platform
  compiler to reject an incompatible component/backend pairing before solving;
- saturation-pair surface tension as an explicit `surface_tension` capability and interfacial
  `SaturationResult` value in N/m; ideal-gas packages do not advertise it and no fallback constant
  is inferred;
- SI-unit density, energy, enthalpy, entropy, heat capacities, speed of sound,
  viscosity, thermal conductivity, vapor quality, and phase;
- validity limits and explicit invalid-input, range, saturation-boundary,
  non-convergence, and backend-error states.
- implementation identity and version for reproducible run provenance.

Every registered backend also supplies catalog metadata: backend ID, implementation name/version,
supported substances where applicable, and capabilities. `thermox_service` publishes separate
fixed-fluid and thermochemistry backend catalogs through `thermox.catalog/v14`, allowing graph
clients to reject unsupported backend/component combinations before simulation submission.

CoolProp 8.0.0 is pinned under `modules/properties/coolprop` and is the primary
external real-fluid implementation. `coolprop_heos` accepts any pure-fluid
identifier supported by the pinned provider and validates it when the model's
medium package is created. Its catalog intentionally publishes an empty
`supported_substances` list to mean provider-open, so declaration clients accept
a provider substance identifier rather than presenting a false closed list.
CO2 can also use the named `co2_span_wagner` registration. Water and steam can
use either `coolprop_if97`/`water_steam_if97` for the industrial IF97 formulation
or generic `coolprop_heos` for a smooth Helmholtz-energy formulation suitable for
regime-spanning inventory transients. The backend, substance, and package version
are explicit and preserved in run provenance.

`coolprop_incompressible` exposes provider-backed single-phase heat-transfer
liquids through the same PT/PH/PS and transport contract. Its first registered
substance is `SolarSalt` (canonical CoolProp fluid `NaK`), the 60 mass-% NaNO3 /
40 mass-% KNO3 mixture correlated over 573.15--873.15 K. It deliberately does
not advertise saturation, surface tension, or native analytic PH derivatives;
components receive the shared bounded PH finite-difference fallback when they
need state derivatives. The property family is generic even though the first
validation consumer is the Solar Two benchmark.

`TabulatedIncompressiblePropertyPackage` provides the corresponding
source-qualified table path. It validates positive, finite, strictly ordered
samples; linearly interpolates density, heat capacity, viscosity, and thermal
conductivity; and analytically integrates piecewise-linear heat capacity for
consistent enthalpy and entropy. `sandia_solar_salt_table` is the first
registration, using SAND2001-2100 Table 1-1 over 500--1100°F. It covers the
Solar Two 290°C cold boundary without weakening or extrapolating CoolProp's
separate NaK validity contract.

## Verification

Regression tests cover known upstream points, PT-to-PH and PT-to-PS round trips, invalid and
out-of-range inputs, steady Newton solves through every backend, a
property-backed transient energy balance through the DAE integrator, and an
end-to-end supercritical-CO2 compressor compiled from the model schema. Platform coverage also
integrates a rigid ideal-gas fluid inventory whose mass and internal-energy closures are evaluated
through the same PH interface used by real-fluid backends. IF97 PH mixture states additionally
drive quality-target evaporator/condenser models and a complete pump–evaporator–turbine–condenser
Rankine graph regression.

The `thermox_platform` compiler resolves each `media[].backend` through
`PropertyPackageRegistry` and injects the resulting package into fluid
components. Each component declares its required property capabilities; compilation fails with a
specific diagnostic when a selected backend cannot supply one. Turbomachinery uses checked
PH/PS equations, allowing range and
convergence failures during line search to be treated as recoverable solver
evaluations rather than process-level exceptions.

## Solver-facing PH derivatives

`state_ph_derivatives` is an explicit capability rather than an assumption attached to every
property backend. The built-in ideal-gas package evaluates the sixteen partials from its closed-form
equations. The HEOS CO2 adapter obtains them from CoolProp's analytic first-partial interface
after one PH update. Both report `PropertyDerivativeSource::analytic`.

CoolProp's IF97 backend does not implement the generic derivative primitives required by that
interface. Thermox therefore does not advertise analytic PH derivatives for IF97. The shared
`state_ph_derivatives_with_fallback` utility evaluates a bounded central difference, with
one-sided behavior at a property-domain boundary, and reports
`PropertyDerivativeSource::finite_difference`. It evaluates the four neighboring PH states once
and derives all sixteen partials from them. Neighboring points are selected from the same phase where
possible so a finite-difference stencil does not accidentally average two constitutive regimes.

HEOS pure fluids advertise analytic PH derivatives in single-phase states. CoolProp does not define
those analytic partials inside the saturation dome, so the shared wrapper uses the same bounded
finite-difference contract there and records finite-difference provenance. This fallback is
limited specifically to an analytic `saturation_boundary` result; other analytic backend failures
remain visible.

Components consume this shared contract. The rigid fluid volume uses density and internal-energy
partials in its DAE closure, map-based turbomachinery uses temperature partials in its
corrected-coordinate chain rule, and safe expression components use entropy partials for
user-declared isentropic closures, bounded two-phase quality partials for quality targets,
heat-capacity partials for thermal correlations, and constant-volume heat-capacity plus
speed-of-sound partials for compressible-flow closures. A third-party backend can replace the
fallback simply by
advertising and implementing the analytic capability; no component changes are required.

Transport-property p-h derivatives use a separate provider-neutral result contract. A package
must advertise both `state_ph` and `transport`; Thermox then constructs bounded central or
same-phase one-sided derivatives for dynamic viscosity and thermal conductivity. Non-positive or
non-finite transport values fail explicitly. The result records finite-difference provenance and
does not weaken or misrepresent the separate analytic thermodynamic derivative capability. Safe
expression components use this path for custom Reynolds/Prandtl, pressure-loss, and heat-transfer
closures.

## Saturation contract and next extensions

The property interface and both real-fluid adapters expose an explicit
`saturation_p` capability returning saturated-liquid and saturated-vapor
states at a common pressure and temperature. The query is accepted from the
fluid triple pressure up to, but not including, the critical pressure. This
includes high-pressure water saturation and near-critical CO2 saturation. The PT
call rejects an exactly saturated state as ambiguous, while PH and PS can
represent a two-phase mixture with vapor quality.

Each worker thread owns one cached CoolProp state object per backend/substance pair,
avoiding shared mutable backend state while amortizing factory cost. Broader independent reference
vectors, an analytic IF97 derivative implementation, and performance profiling
remain useful promotion work for production-scale simulations.
