# Dynamic material-to-fluid heat exchanger

`heat_exchanger.material_fluid.dynamic_cell` is a generic bridge between a composition-aware hot
material stream and a property-backed cold fluid. It is intended for exhaust-to-water heat
recovery, recuperation, reactor coolant interfaces, and other mixed-domain exchangers without
embedding a gas-turbine, HRSG, or cycle-specific solver path.

The hot ports use the `material` connector and require a thermochemistry package with `state_ph`.
The cold ports use the `fluid` connector and require a property package with `state_ph`. The
component conserves every species flow declared by the bound material and never converts the hot
stream into a pseudo-fluid.

## Physical model

The hot side is quasi-steady. For total material flow `m_hot`, its equations conserve each species,
apply a local pressure-loss correlation, and transfer enthalpy to the wall:

```text
m_hot (h_hot,in - h_hot,out) = UA_hot (T_hot,out - T_wall)
p_hot,in - p_hot,out = K_hot m_hot |m_hot| / (2 rho_hot A_hot^2)
```

The cold side is a well-mixed, constant-mass inventory with stored total internal energy `U_cold`:

```text
dU_cold/dt = m_cold h_cold,in - m_cold h_cold - Q_cold
U_cold = M_cold u(p_cold, h_cold)
Q_cold = UA_cold (T_wall - T_cold)
```

The wall adds a second differential state:

```text
C_wall dT_wall/dt = Q_hot - Q_cold
```

At steady state the storage derivatives vanish and hot-side duty, cold-side duty, and wall heat
balance close algebraically. Both flow paths use their own diameter and local-loss coefficient.

Thermochemistry-dependent residual derivatives are evaluated with bounded finite differences while
retaining a fixed sparse DAE pattern. This keeps arbitrary registered mechanisms behind the common
contract. Its evaluation cost grows with the declared species basis, so mechanism reduction or
analytic backend derivatives remain valid later optimizations rather than architectural changes.

## Runnable Cantera-to-IF97 reference

The reference sends a four-species exhaust mixture through the hot material path and IF97 liquid
water through the cold path:

```sh
./build/thermox_cli simulate \
  --model core/examples/dynamic_exhaust_water_hrsg_cell.json \
  --case heat_up \
  --end-time 0.1 \
  --format json
```

Regression coverage verifies real Cantera/IF97 service compilation, consistent DAE initialization,
adaptive integration, exact N2/O2/H2O/CO2 flow conservation, exhaust cooling, water heating, and
stored-energy closure. The example is an isolated heat-recovery cell, not a calibrated full HRSG.

## Scope and next extension

The hot gas has no inventory or pressure-wave dynamics; axial conduction, radiation, ambient loss,
fouling evolution, and flow reversal are outside this first model. A distributed single-pressure
HRSG can be declared by composing cells in counterflow. Credible evaporator dynamics additionally
need an appropriate two-phase cell or drum/circulation topology with void-fraction and phase-change
correlations. Those belong in registered generic component models, not in case-specific code.
