# Distributed two-phase riser

Thermox composes the open boiling-riser reference in
`core/examples/dynamic_distributed_two_phase_riser.json` from ordinary registered components:

```text
exhaust -> lower equilibrium cell -> upper equilibrium cell -> stack
                    |                         |
feed -> lower cell -+-> slip riser -> upper -+-> slip riser -> outlet
                         (+5 m)                    (+5 m)
```

There is no riser, HRSG, or boiler-specific solve path. Each heat-transfer cell owns a rigid
two-phase inventory and wall state. Each pipe owns only isenthalpic transport and its hydraulic
closure. Connections assemble the distributed DAE.

Run the reference segment with:

```bash
./build/thermox_cli simulate \
  --model core/examples/dynamic_distributed_two_phase_riser.json \
  --case heatup_segment \
  --end-time 0.1
```

## Constant-slip hydraulic closure

`pipe.fluid.constant_slip_two_phase_local_loss` defines the slip ratio as
`S = velocity_vapor / velocity_liquid`. At mean endpoint pressure, registered `state_ph` and
`saturation_p` capabilities provide mass quality `x` and saturated phase densities. The model
calculates

```text
alpha = 1 / [1 + ((1-x)/x) (rho_v/rho_l) S]
rho_mix = alpha rho_v + (1-alpha) rho_l
p_in - p_out = K m_dot |m_dot| / (2 rho_mix A^2) + rho_mix g delta_z
```

`alpha` is vapor void fraction and `delta_z` is outlet elevation minus inlet elevation. `S = 1`
recovers the homogeneous void-fraction relation. The signed friction term remains continuous at
zero flow. The component is available in steady and transient graphs and requires a strictly
two-phase mean state (`0 < x < 1`); incompatible states are reported as recoverable physical-model
evaluation failures.

## Reference verification

The illustrative case begins with lower/upper cell qualities of about 0.4405/0.4867. After 0.1 s,
both remain two phase and rise to about 0.4417/0.4878. Independent cell pressures and the
inter-cell flow evolve through the two elevation/loss equations. Every exhaust species remains
conserved.

At the final sample, the declared net water inflow is 0.0002 kg/s and exactly equals the sum of
both inventory mass derivatives. The two fluid-energy storage rates plus both wall-storage rates
sum to the graph boundary energy flow (about 176.8 kW, within numerical integration tolerance).
The automated component test separately reconstructs void fraction and pressure loss from IF97
phase properties, checks isenthalpic mass transport, and verifies consistent DAE initialization.

Geometry, conductances, slip ratios, and boundary conditions are verification inputs, not
calibrated equipment data.

## Fidelity boundary

Constant slip is a transparent baseline closure, not a universal two-phase correlation. The model
does not yet include drift flux, flow-regime maps, two-phase friction multipliers, acceleration
pressure drop, boiling crisis, dryout, flashing fronts, or regime switching. The equilibrium cells
also assume one saturated state per cell. Engineering prediction requires selecting validated
correlations for the geometry and operating regime.

`pipe.fluid.void_fraction_correlation_local_loss` now provides the versioned, immutable
correlation-artifact extension point for engineer-supplied void-fraction laws. It consumes live
quality, phase-density, geometry, pressure, and flow inputs through a dimension-checked contract.
Supplying the correlation does not by itself add regime selection, applicability limits, or
validation data; those remain explicit engineering responsibilities and future platform work.
