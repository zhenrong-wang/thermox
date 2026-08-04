# Dynamic single-pressure HRSG

`core/examples/dynamic_single_pressure_hrsg.json` is a declaration-only composition of generic
registered components. It is not implemented by an HRSG-specific solver or component class.

The exhaust path follows the temperature ordering used by a single-pressure heat-recovery steam
generator:

```text
exhaust source -> superheater -> evaporator -> economizer -> stack
```

The counterflow water/steam path is:

```text
feedwater -> economizer -> evaporator -> two-phase local loss
          -> equilibrium separator drum -> superheater -> steam sink
                                      `-> blowdown sink
```

The economizer and superheater are instances of
`heat_exchanger.material_fluid.dynamic_cell`. The evaporator is an
`heat_exchanger.material_fluid.equilibrium_two_phase_cell`, and the separator is an independent
`drum.fluid.equilibrium_two_phase` inventory. A
`restriction.fluid.local_loss.homogeneous_two_phase` component provides physical hydraulic
impedance between those two stored volumes.

## Why the restriction is explicit

A rigid equilibrium inventory's mass and internal energy determine its pressure and quality.
Connecting two such inventories through an ideal zero-pressure-drop link forces their independent
pressure closures to be identical. During DAE consistent initialization that produces a dependent
constraint unless the stored states happen to lie exactly on the shared manifold.

Real connecting tubes, headers, and entries have hydraulic impedance. Representing it explicitly
lets the pressure difference determine two-phase transfer flow and keeps component ownership clear:
the heat-transfer cell owns storage and heat transfer; the restriction owns hydraulic loss; the
drum owns separation and its inventory.

## Regression scope

The `startup_segment` case integrates 0.1 seconds with real Cantera exhaust thermochemistry and
CoolProp IF97 water properties. Automated verification checks:

- monotonically falling exhaust temperature through superheater, evaporator, and economizer;
- exact N2, O2, H2O, and CO2 transport from exhaust source to stack;
- liquid feedwater, two-phase evaporator discharge, saturated-vapor separation, and superheated
  steam discharge;
- positive forward flow and pressure drop through the two-phase impedance;
- combined evaporator/drum mass accumulation against the system boundary mass rate;
- all fluid, drum, and wall energy-storage derivatives against the system boundary enthalpy rate.

The example's sizes and coefficients are illustrative engineering inputs chosen to exercise the
coupled equations. This is a platform-integration reference, not an OEM HRSG design, performance
prediction, or calibration benchmark.

## Validity boundary

The model assumes lumped cells, quasi-steady exhaust flow, homogeneous equilibrium in the
evaporator and connecting loss, perfect phase separation in the drum, and no ambient heat loss.
It does not yet include natural/forced circulation loops, riser/downcomer slip, shrink/swell,
pinch/approach design constraints, tube metal segmentation beyond one wall state per cell,
radiation, fouling, attemperation, safety valves, or phase-regime switching. Those capabilities
remain separate generic component or solver extensions rather than reasons to specialize the
platform around this example.
