# Dynamic natural-circulation loop

Thermox composes an elevation-driven water/steam circuit in
`core/examples/dynamic_natural_circulation_evaporator.json`:

```text
                          +---- two-phase riser (+10 m) ----+
                          |                                 |
drum liquid -> downcomer (-10 m) -> inertance -> evaporator +-> drum
                                                        ^
exhaust source -----------------------------------------+
```

The model contains no pump and no natural-circulation-specific solver branch. It composes the
registered IF97 property package, Cantera exhaust, equilibrium drum, equilibrium boiling cell,
gravity/local-loss pipe segments, and lumped hydraulic inertance.

Run the reference segment with:

```bash
./build/thermox_cli simulate \
  --model core/examples/dynamic_natural_circulation_evaporator.json \
  --case buoyancy_segment \
  --end-time 0.1
```

## Gravity and friction closure

`pipe.fluid.homogeneous_equilibrium_local_loss` evaluates density from the registered fluid
property package at mean endpoint pressure and the transported enthalpy. It solves

```text
p_in - p_out = K m_dot |m_dot| / (2 rho A^2) + rho g delta_z
A = pi D^2 / 4
```

`delta_z` is outlet elevation minus inlet elevation. An upward segment therefore has positive
hydrostatic loss; a downward segment has pressure recovery. The signed friction term supports
forward flow, reverse flow, and the zero-flow limit without changing the declared port topology.
The component accepts liquid, vapor, and homogeneous-equilibrium two-phase property states and is
available in steady and transient graphs.

Gravity/friction remains an algebraic constitutive model. The independent
`pipe.fluid.hydraulic_inertance` component supplies the loop momentum state. Keeping them separate
lets engineers combine different loss correlations, elevations, and momentum lumping without
embedding a particular boiler topology in either component.

## Reference behavior and verification

In the declared reference, saturated drum liquid enters the downward leg at roughly 944 kg/m3,
while the heated riser mixture is only a few kg/m3. The resulting density-head imbalance raises
downcomer pressure, overcomes the declared losses, and accelerates positive flow without shaft
work.

Automated verification checks:

- no pump exists in the graph;
- liquid downcomer and two-phase riser states resolve through IF97;
- downward pressure recovery and upward pressure loss have the correct signs;
- loop flow remains positive and accelerates from its initial state;
- reverse signed flow solves independently in steady and transient component contracts;
- exhaust temperature falls and every declared exhaust species is conserved;
- evaporator and drum mass accumulation cancel exactly across the closed water circuit;
- combined fluid, drum, and wall energy storage equals the exhaust energy boundary, with no hidden
  pump work.

The example geometry and loss coefficients are illustrative engineering inputs selected to test
the coupled equations. They are not calibrated boiler data.

## Fidelity boundary

This is a lumped homogeneous-equilibrium natural-circulation model. It does not represent separate
liquid/vapor velocities, void-fraction correlations, drift flux, flow-regime maps, boiling crisis,
critical heat flux, flashing fronts, distributed pressure/enthalpy cells, parallel-channel
instability, Ledinegg instability, or density-wave oscillations. Kinetic and potential energy are
not included in the current stream-energy audit. Although the hydraulic pipe contract is signed,
every component in a reversed system path must independently support reversal before a complete
plant model can cross through zero flow safely.
