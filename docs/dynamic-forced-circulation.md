# Dynamic forced-circulation loop

Thermox composes a closed water/steam circulation circuit in
`core/examples/dynamic_forced_circulation_evaporator.json`:

```text
drum liquid -> circulation pump -> hydraulic inertance -> evaporator
     ^                                                   |
     +------------ two-phase riser loss <---------------+

exhaust source -> evaporator -> stack
drum vapor -> closed steam boundary
```

The graph is declaration-only. It uses the registered IF97 property package, Cantera exhaust,
equilibrium drum, equilibrium two-phase heat-transfer cell, fixed-efficiency pump, homogeneous
two-phase resistance, and generic hydraulic inertance. There is no circulation-loop or evaporator
solver branch.

Run the short dynamic segment with:

```bash
./build/thermox_cli simulate \
  --model core/examples/dynamic_forced_circulation_evaporator.json \
  --case circulation_segment \
  --end-time 0.1
```

## Momentum closure

A closed fluid circuit cannot generally be initialized from rigid inventories and quasi-steady
pressure equations alone. Loop flow has momentum storage. The transient-only
`pipe.fluid.hydraulic_inertance` component supplies the lumped one-dimensional balance

```text
p_in - p_out = (L / A) d(m_dot)/dt
A = pi D^2 / 4
```

while enforcing mass continuity and isenthalpic transport. `length` and `flow_diameter` are
ordinary component parameters. Friction and local loss remain separate components, allowing an
engineer to select correlations independently of momentum storage.

The outlet mass-flow connector variable owns the differential state. Connected algebraic flow
variables follow it through ordinary connector equalities. This preserves the platform's typed
graph contract and leaves room for distributed momentum cells or higher-fidelity pipe models.

## Quasi-steady equipment in a dynamic graph

The fixed-isentropic-efficiency fluid pump now provides an algebraic transient form: its pressure
ratio, efficiency, mass continuity, and shaft power are solved at every DAE stage without adding
artificial equipment storage. Ideal two-inlet fluid mixers and two-outlet splitters likewise expose
steady and transient algebraic forms. Their physical contracts are unchanged between solve modes.

## Verification

The service regression checks:

- positive pump-driven circulation and positive pressure losses;
- a resolved positive loop-flow acceleration from the inertance equation;
- liquid drum return and two-phase evaporator/riser states;
- exhaust cooling and exact conservation of every declared exhaust species;
- equal-and-opposite evaporator/drum water accumulation in the closed circuit;
- combined evaporator-fluid, wall, and drum energy storage against exhaust heat and pump work at
  the system boundary.

The reference is an architectural and conservation test. Its geometry, pump ratio, loss
coefficient, and heat-transfer parameters are illustrative, not calibrated equipment data.

## Current validity boundary

The model uses lumped one-dimensional momentum, a fixed pump pressure ratio and efficiency,
homogeneous two-phase resistance, equilibrium phase storage, and no vapor withdrawal during the
short closed-loop segment. Consistent with the platform's current stream convention, kinetic
energy storage is neglected in the thermal-energy audit. It does not yet model elevation-driven natural circulation,
two-fluid slip, flow-regime transitions, pump performance maps in transient mode, cavitation,
check-valve behavior, or shrink/swell. Those remain generic capability extensions.
