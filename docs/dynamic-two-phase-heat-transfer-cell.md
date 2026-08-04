# Dynamic two-phase heat-transfer cell

`heat_exchanger.material_fluid.equilibrium_two_phase_cell` couples a composition-aware hot material
stream to a rigid, well-mixed saturated fluid inventory. It is a transient-only physical model for
boiling or condensing control volumes and is not tied to an HRSG, steam cycle, or particular fluid.

The hot path uses `material` ports and a thermochemistry package with `state_ph`. The fluid path uses
`fluid` ports and a property package with `saturation_p`. The reference binds Cantera exhaust and
IF97 water, but the contracts remain registry-driven.

## State and conservation model

The differential states are fluid mass `M`, fluid total internal energy `U`, and wall temperature
`T_wall`. Pressure `p` and mass vapor quality `x` are algebraic states. Saturated liquid/vapor
properties at `p` close the fixed volume and stored energy:

```text
M [(1-x)/rho_l(p) + x/rho_v(p)] = V
U = M [(1-x) u_l(p) + x u_v(p)]
```

The balances are:

```text
dM/dt = m_in - m_out
dU/dt = m_in h_in - m_out h_mix + Q_wall_to_fluid
C_wall dT_wall/dt = Q_hot_to_wall - Q_wall_to_fluid
```

The outlet is the equilibrium mixture:

```text
h_mix = (1-x) h_l(p) + x h_v(p)
T_mix = T_sat(p)
```

The hot material path is quasi-steady and conserves every species. Its pressure loss and enthalpy
drop are evaluated with the selected thermochemistry package. Fluid inlet/outlet mass flows and
inlet enthalpy are boundary/topology inputs; both fluid-port pressures equal inventory pressure.
Pumps, valves, circulation paths, and external restrictions should be declared as separate
components.

## Runnable reference

```sh
./build/thermox_cli simulate \
  --model core/examples/dynamic_two_phase_rigid_volume_cell.json \
  --case boil \
  --end-time 1.0
```

The declared case begins at 2 bar and vapor quality 0.4521. Over one second, exhaust heating raises
quality to about 0.4850 and pressure to about 2.156 bar while the small positive net inflow increases
inventory mass by 0.1 g. These are verification values from illustrative geometry and conductance,
not equipment predictions.

Regression coverage checks exact exhaust-species conservation, rigid-volume closure, saturation
quality consistency, integrated mass accumulation, and equality of fluid-plus-wall storage rate
with the graph's net boundary energy flow.

## Validity limits

This model is valid only while `0 <= x <= 1`. It assumes homogeneous equilibrium, no phase slip,
no axial gradients, and no explicit nucleate-boiling, dryout, or condensation heat-transfer
correlation. It does not model a drum level or natural-circulation loop. Crossing into subcooled or
superheated regimes requires either event-driven switching to a single-phase cell or a future
regime-spanning finite-volume model. Multi-cell evaporator and drum/circulation topology can be
assembled without changing the numerical kernel.
