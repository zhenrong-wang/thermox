# Regime-spanning rigid fluid volume

`volume.fluid.rigid_heat_transfer` is the general equilibrium fluid-inventory
component for transient graphs. It complements
`volume.fluid.rigid_adiabatic` with a heat port while retaining the same
conserved states and property contract.

The differential states are total mass `M` and total internal energy `U`:

```text
dM/dt = m_in - m_out
dU/dt = m_in h_in - m_out h_out + Q_dot
```

Pressure and specific enthalpy remain algebraic states. At every DAE stage the
selected property package closes

```text
M = rho(p, h) V
U = M u(p, h)
```

This formulation does not encode a liquid-only or vapor-only component. Phase
identity is a result of the registered property package, so the same declared
topology can evolve through a property regime boundary. Adaptive local-error
control is applied to differential states only; algebraic pressure, enthalpy,
temperature, and phase readouts are solved at each accepted stage but do not
incorrectly dictate the time step.

## Verified reference path

[`dynamic_regime_spanning_rigid_volume.json`](../core/examples/dynamic_regime_spanning_rigid_volume.json)
starts IF97 water immediately below the saturated-vapor boundary, adds heat,
and discharges through a hydraulic inertance to a regulated-pressure sink. A
ten-second run reaches the vapor region while retaining mass and energy
inventory equations:

```sh
./build/thermox_cli simulate \
  --model core/examples/dynamic_regime_spanning_rigid_volume.json \
  --case two_phase_to_vapor \
  --end-time 10 --format json
```

The regression is intentionally small and CPU-bounded. It verifies the generic
graph compiler, IF97 PH closure, heat-port energy term, hydraulic momentum
state, consistent initialization, and adaptive DAE integration together.

## Validity and current limit

The component represents one homogeneous equilibrium bulk state. It does not
model stratification, a resolved interface, nucleation hysteresis, metastable
states, critical heat flux, dryout, or flow-regime switching. Those effects
belong in specialized registered components and correlations.

The IF97 bounded finite-difference PH derivative fallback is currently robust
for the verified two-phase-to-vapor crossing. Liquid/two-phase crossings remain
a declared limitation: the derivative surface at that boundary can make the
current Newton/BDF1 path singular. Thermox reports solver failure rather than
loosening conservation tolerances or silently selecting a phase branch. Native
IF97 derivatives or a phase-aware state-variable transformation is required
before claiming bidirectional saturation-dome traversal.
