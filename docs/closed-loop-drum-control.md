# Closed-loop drum feed control

`core/examples/closed_loop_drum_control.json` is an integrated dynamic reference model. It is not a
special-purpose cycle implementation: every node is selected from the component registry and every
connection is declared through a typed connector.

The graph is:

```text
water boundary -> actuated non-flashing valve -> equilibrium two-phase drum
                                                    |
                                                    v
                                              normalized level
                                                    |
                                                    v
feed-valve command <- first-order actuator <- bounded PI controller
```

This one model composes six platform concerns:

- an IF97 water/steam property package;
- connected fluid pressure, enthalpy, and mass-flow variables;
- differential drum mass and internal-energy inventories;
- algebraic saturation pressure, quality, and liquid-level closure;
- typed measurement and control signals; and
- bounded feedback control and actuator dynamics in the transient DAE.

Run it with:

```sh
./build/thermox_cli simulate \
  --model core/examples/closed_loop_drum_control.json \
  --case level_control \
  --end-time 0.5 \
  --format json
```

The supplied case starts near 2 bar and 10% vapor mass quality. Its feed-water boundary is fixed at
20 bar and 300 kJ/kg, its vapor outlet is closed, and its liquid discharge is fixed at 0.05 kg/s.
The controller regulates normalized liquid level by changing feed-valve area through an explicit
first-order actuator. The short run intentionally exercises coupled initialization and response;
it is not a tuned statement about the behavior of a particular industrial drum.

## Initialization contract

The drum's total mass and internal energy are differential states. Pressure, vapor quality, and
level are algebraic states that must be made consistent with those inventories and the vessel
volume before integration. The controller integral and actuator response are additional
differential states.

Useful initial guesses are also supplied for the connected valve pressures and enthalpies. They are
not fixed boundaries and do not over-constrain the model. They keep the initial nonlinear trial in
the valve's declared non-flashing flow direction while the consistent initializer converges. A
downstream-pressure trial above the feed pressure is outside that component's validity domain and
is rejected rather than being silently evaluated with an invalid square root or reversed-flow
assumption.

These guesses illustrate a general platform rule: differential initial conditions define stored
state, fixed values define external boundaries, and algebraic guesses guide consistent
initialization without becoming physical constraints.

## Scope and limits

The example validates composition of the graph, registry, property, control, and DAE layers. It
does not validate industrial drum geometry, valve sizing, flashing discharge, circulation,
heat-transfer surfaces, sensor dynamics, or a plant control tuning. Those belong in component
instances and engineering data for a particular system. The feed valve is deliberately used on
subcooled feed water because its registered model rejects flashing-liquid service.
