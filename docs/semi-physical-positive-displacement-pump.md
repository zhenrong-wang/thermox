# Semi-physical positive-displacement pump

`pump.fluid.semi_physical_positive_displacement` represents diaphragm, piston, gear, and other
approximately positive-displacement pumps without requiring an arbitrary rectangular performance
map. It is a generic real-fluid, parameter-driven component; it does not embed a refrigerant,
machine, or benchmark.

The ideal volumetric capacity is the declared displacement per revolution multiplied by rotational
frequency. A lumped incompressible-orifice leakage path opposes delivery as discharge pressure
rises:

```text
m_ideal = rho_in * V_displacement * omega / (2*pi)
m_leak  = Cd * A_leak * sqrt(2 * rho_in * (p_out - p_in))
m_out   = m_ideal - m_leak
```

The active fluid package supplies inlet density and the isentropic outlet state. Isentropic
efficiency closes outlet enthalpy and positive shaft input power:

```text
h_out = h_in + (h_out,is - h_in) / eta_is
W_shaft = m_out * (h_out - h_in)
```

The component exposes `ideal_displacement_mass_flow` and `leakage_mass_flow` diagnostics. It
supports steady graphs and a quasi-steady closure inside transient graphs. The public
`evaluate_semi_physical_positive_displacement_pump` function is the canonical direct path for
calibration and optimization, and the registered graph component delegates to the same evaluator.

## Parameters and interpretation

- `displacement_volume_per_revolution`: swept or effective delivered volume before leakage.
- `leakage_area`: lumped effective leakage area.
- `leakage_discharge_coefficient`: leakage coefficient in `[0, 1]`.
- `eta_is`: aggregate isentropic efficiency used for fluid heating and shaft input.

Calibration from operating data generally identifies the product `Cd * A_leak`, not the two terms
independently. Likewise, a fitted effective displacement can include valve timing, dead volume,
and other unmodeled capacity losses. Those fitted quantities must not be presented as measured
geometry.

The model requires forward pumping (`p_out >= p_in`), positive speed, positive delivered flow, and
a single fluid on both ports. It deliberately omits pulsation, check-valve dynamics, cavitation,
compressibility within chambers, and speed-dependent mechanical loss. Those effects should be
introduced as richer component models when the corresponding data are available, rather than as
hidden case corrections.
