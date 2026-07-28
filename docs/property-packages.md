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
supported substances, and capabilities. `thermox_service` publishes this metadata through
`thermox.catalog/v1`, allowing graph clients to reject unsupported fluid/backend/component
combinations before simulation submission.

CO2 and IF97 remain independent Git repositories under `modules/properties/`.
Their exported C APIs are namespaced, while legacy implementation symbols are
hidden inside separate shared libraries. This prevents symbol interposition
between the two old C implementations.

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

The property interface and both real-fluid backends expose an explicit
`saturation_p` capability returning saturated-liquid and saturated-vapor
states at a common pressure and temperature. IF97 currently supports this
query from its minimum saturation pressure through the Region 1/2 boundary
(approximately 16.5 MPa); higher-pressure Region 3 saturation remains future
backend work. The PT call still rejects an exactly saturated state as
ambiguous, while PH can represent an interior two-phase mixture with vapor
quality.

Solver-facing analytic derivatives, thread-safety stress testing, broader
authoritative IAPWS/Span-Wagner vectors, and caching policy remain promotion
work for production-scale simulations.
