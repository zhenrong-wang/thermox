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
  temperature, density, and internal-energy partials with respect to pressure and enthalpy;
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
fixed-fluid and thermochemistry backend catalogs through `thermox.catalog/v10`, allowing graph
clients to reject unsupported backend/component combinations before simulation submission.

CoolProp 8.0.0 is pinned under `modules/properties/coolprop` and is the only
real-fluid implementation. CO2 is evaluated with the high-accuracy HEOS
backend using its Span-Wagner formulation. Water and steam can use either
`coolprop_if97`/`water_steam_if97` for the industrial IF97 formulation or
`coolprop_heos` for a smooth Helmholtz-energy formulation suitable for
regime-spanning inventory transients. The backend choice is explicit and is
preserved in run provenance.

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
property backend. The built-in ideal-gas package evaluates the six partials from its closed-form
equations. The HEOS CO2 adapter obtains them from CoolProp's analytic first-partial interface
after one PH update. Both report `PropertyDerivativeSource::analytic`.

CoolProp's IF97 backend does not implement the generic derivative primitives required by that
interface. Thermox therefore does not advertise analytic PH derivatives for IF97. The shared
`state_ph_derivatives_with_fallback` utility evaluates a bounded central difference, with
one-sided behavior at a property-domain boundary, and reports
`PropertyDerivativeSource::finite_difference`. It evaluates the four neighboring PH states once
and derives all six partials from them. Neighboring points are selected from the same phase where
possible so a finite-difference stencil does not accidentally average two constitutive regimes.

HEOS water advertises analytic PH derivatives in single-phase states. CoolProp does not define
those analytic partials inside the saturation dome, so the shared wrapper uses the same bounded
finite-difference contract there and records finite-difference provenance. This fallback is
limited specifically to an analytic `saturation_boundary` result; other analytic backend failures
remain visible.

Components consume this shared contract. The rigid fluid volume uses density and internal-energy
partials in its DAE closure, while map-based turbomachinery uses temperature partials in its
corrected-coordinate chain rule. A third-party backend can replace the fallback simply by
advertising and implementing the analytic capability; no component changes are required.

## Saturation contract and next extensions

The property interface and both real-fluid adapters expose an explicit
`saturation_p` capability returning saturated-liquid and saturated-vapor
states at a common pressure and temperature. The query is accepted from the
fluid triple pressure up to, but not including, the critical pressure. This
includes high-pressure water saturation and near-critical CO2 saturation. The PT
call rejects an exactly saturated state as ambiguous, while PH and PS can
represent a two-phase mixture with vapor quality.

Each worker thread owns its CoolProp state objects, avoiding shared mutable
backend state while amortizing factory cost. Broader independent reference
vectors, an analytic IF97 derivative implementation, and performance profiling
remain useful promotion work for production-scale simulations.
