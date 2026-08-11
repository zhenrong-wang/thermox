# Segmented triple-pressure reheat HRSG

`benchmarks/netl_b31a/segmented_hrsg.json` is a steady, declaration-only decomposition of the
published NETL B31A HRSG boundary. It proves that a multi-pressure reheat HRSG is ordinary Thermox
topology assembled from registered heat exchangers, boundaries, a valve, and a mixer. There is no
HRSG-specific component or solve path.

## Declared topology

The composition-aware exhaust path crosses ten independently parameterized surfaces:

```text
exhaust -> HP SH -> RH -> IP SH -> LP SH -> HP EV -> IP EV -> LP EV
        -> HP ECO -> IP ECO -> LP ECO -> stack
```

Three IF97 circuits traverse separate economizer, evaporator, and superheater instances. The IP
make-up steam mixes with pressure-equalized cold reheat before the reheater:

```text
HP feedwater -> HP ECO -> HP EV -> HP SH --------------------> main steam
IP feedwater -> IP ECO -> IP EV -> IP SH --.
cold reheat -> isenthalpic pressure equalizer -------------+-> mixer -> RH -> hot reheat
LP feedwater -> LP ECO -> LP EV -> LP SH --------------------> LP steam
```

The graph contains 21 reusable component instances and solves 250 variables simultaneously. Every
exhaust species is transported through every surface. Cantera resolves the exhaust states and
CoolProp IF97 resolves liquid, near-saturated, and superheated water/steam states.

## Boundary decomposition

The public report does not provide coil-by-coil duties. For this topology regression, intermediate
enthalpy targets partition the published boundary duty as follows:

| Circuit | Economizer (MW) | Evaporator (MW) | Superheater (MW) | Reheater (MW) |
|---|---:|---:|---:|---:|
| HP | 231.860405 | 135.571500 | 138.661024 | - |
| IP | 12.074867 | 23.763542 | 5.431667 | 81.633806 |
| LP | 9.214069 | 40.530984 | 6.228659 | - |

The sum is 684.970522 MW. It differs by about 45 W from the published-stream reconstruction because
the displayed water/steam flows have a 1 kg/h mass-rounding imbalance. The regression preserves the
three active circuit flows instead of manufacturing an unreported correction.

Each of ten equal fractional gas-side pressure losses compounds from the published 0.11 MPa inlet
to exactly 0.10 MPa at the stack. With the boundary duty applied, the segmented graph predicts the
same 357.686 K stack state as the aggregate benchmark.

## Verification and evidence strength

Automated service verification checks:

- nonlinear continuation reaches the full topology with a normalized residual below `1e-10`;
- exhaust temperature falls across every declared surface and all five species are conserved;
- HP, IP, and LP circuits progress through their declared heating/evaporation states;
- the IP make-up and cold-reheat mixer closes mass and energy exactly;
- main-steam, hot-reheat, and LP-steam boundary states are reproduced;
- gas-side surface duty and whole-system mass/energy balances close independently.

Those checks do not make the assumed coil arrangement physically admissible. The generic
[`thermox.thermal_feasibility/v1`](thermal-feasibility-architecture.md) counterflow audit detects
negative terminal approaches in the assumed reheater/economizer ordering even though the nonlinear
equations and energy balances close. This is an intentional regression: Thermox must expose the
surface-level failure instead of allowing numerical success to mark the model calculation-ready.

The machine-readable validation evidence deliberately classifies the stack comparison as
`boundary_constrained`, the three steam outputs as `calibrated_reproduction`, and equation closure
as `internal_consistency`. It records zero `independent_reference` claims.

## Validity boundary

This model validates generic graph composition, multi-pressure routing, phase-aware properties,
serial gas coupling, mixing, conservation, and rejection of thermally infeasible surface ordering.
It is not a coil-design or off-design HRSG model.
The source does not publish the actual surface order, coil duties, geometry, UA values, pinch and
approach temperatures, detailed pressure losses, drum conditions, circulation ratios, blowdown,
attemperation, fouling, or ambient loss. Feed pumps and dynamic inventories are also outside this
steady boundary decomposition; those generic capabilities are verified by separate Thermox models.

Replacing fixed duties with UA/geometry correlations and adding drums or dynamic cells requires
only different registered component instances and engineering inputs, not a new system solver.
