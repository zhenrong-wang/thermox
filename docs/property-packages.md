# Property Packages

## Reacting mixtures

Fixed-fluid property packages and reacting-mixture thermochemistry are separate Thermox contracts.
`ThermochemistryPackage` owns:

- an ordered species basis and validated mass- or mole-fraction compositions;
- composition-aware PT and PH states;
- composition-aware PS states for isentropic compression and expansion;
- equilibrium at fixed enthalpy and pressure;
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
PH, PS, equilibrium-HP, and transport capabilities. The default build remains bounded and does
not compile Cantera. When the option is enabled, the native service runtime links and registers
the Cantera backend automatically; callers can still construct an explicit runtime registry for
dependency injection and testing.

The built-in `compressor.material.isentropic_efficiency` and
`turbine.material.isentropic_efficiency` components preserve each species mass flow, evaluate the
isentropic outlet through the PS contract, apply isentropic efficiency, and close shaft power.
They use the same component graph semantics as fixed-fluid turbomachinery and contain no
gas-turbine-specific assumptions.

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
- explicit capability discovery for PT, PH, PS, and transport operations, allowing the platform
  compiler to reject an incompatible component/backend pairing before solving;
- SI-unit density, energy, enthalpy, entropy, heat capacities, speed of sound,
  viscosity, thermal conductivity, vapor quality, and phase;
- validity limits and explicit invalid-input, range, saturation-boundary,
  non-convergence, and backend-error states.
- implementation identity and version for reproducible run provenance.

Every registered backend also supplies catalog metadata: backend ID, implementation name/version,
supported substances where applicable, and capabilities. `thermox_service` publishes separate
fixed-fluid and thermochemistry backend catalogs through `thermox.catalog/v4`, allowing graph
clients to reject unsupported backend/component combinations before simulation submission.

CoolProp 8.0.0 is pinned under `modules/properties/coolprop` and is the only
real-fluid implementation. CO2 is evaluated with the high-accuracy HEOS
backend using its Span-Wagner formulation; water and steam use CoolProp IF97.
Thermox keeps its property-package and backend-selection contracts, while no rejected
CO2 or water/steam implementation or fallback path remains.

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

## Saturation contract and next extensions

The property interface and both real-fluid adapters expose an explicit
`saturation_p` capability returning saturated-liquid and saturated-vapor
states at a common pressure and temperature. The query is accepted from the
fluid triple pressure up to, but not including, the critical pressure. This
includes high-pressure water saturation and near-critical CO2 saturation. The PT
call rejects an exactly saturated state as ambiguous, while PH and PS can
represent a two-phase mixture with vapor quality.

Each worker thread owns its CoolProp state objects, avoiding shared mutable
backend state while amortizing factory cost. Solver-facing analytic
derivatives, broader independent reference vectors, and performance profiling
remain useful promotion work for production-scale simulations.
