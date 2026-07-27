# Property Packages

Thermox separates numerical algorithms from thermophysical models:

```text
thermal components and systems
            |
     thermox_physics
  PropertyPackage (SI units)
            |
  +---------+---------+
  |         |         |
ideal gas  CO2      IF97
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

- pressure-temperature (`state_pt`) and pressure-enthalpy (`state_ph`) flashes;
- SI-unit density, energy, enthalpy, entropy, heat capacities, speed of sound,
  viscosity, thermal conductivity, vapor quality, and phase;
- validity limits and explicit invalid-input, range, saturation-boundary,
  non-convergence, and backend-error states.

CO2 and IF97 remain independent Git repositories under `modules/properties/`.
Their exported C APIs are namespaced, while legacy implementation symbols are
hidden inside separate shared libraries. This prevents symbol interposition
between the two old C implementations.

## Verification

Regression tests cover known upstream points, PT-to-PH round trips, invalid and
out-of-range inputs, steady Newton solves through every backend, and a
property-backed transient energy balance through the DAE integrator.

## Deliberate next extensions

Before complex two-phase equipment is considered complete, the contract should
gain explicit saturation-pair queries and solver-facing analytic derivatives.
The current PT call rejects an exactly saturated state as ambiguous; PH can
represent a two-phase mixture with vapor quality. Thread-safety stress testing,
broader authoritative IAPWS/Span-Wagner vectors, and caching policy also remain
promotion work for production-scale simulations.
