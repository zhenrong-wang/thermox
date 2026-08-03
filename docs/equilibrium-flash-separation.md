# Equilibrium flash separation

`separator.fluid.equilibrium_flash` is a generic steady component with one fluid inlet and separate
liquid and vapor outlets. It is property-backed and cycle-independent: water/steam drums,
refrigerant receivers, and other equilibrium separators can use the same component when its
assumptions are appropriate.

The model requires all ports to use the same medium and requires the selected property package to
provide saturation states at pressure. An optional `pressure_loss_fraction` defines the common
separator pressure:

```text
p_separator = (1 - pressure_loss_fraction) p_in
p_liquid = p_vapor = p_separator
```

At separator pressure the backend supplies saturated-liquid enthalpy `h_f` and saturated-vapor
enthalpy `h_g`. The inlet equilibrium quality and outlet flows are

```text
x = (h_in - h_f) / (h_g - h_f)
m_dot_vapor = x m_dot_in
m_dot_liquid = (1 - x) m_dot_in
h_vapor = h_g
h_liquid = h_f
```

These equations are the mass and adiabatic steady-flow energy balances written in lever-rule form.
This formulation avoids a singular initial Jacobian when generic outlet enthalpy guesses happen to
be equal. Saturation-enthalpy and quality pressure derivatives use bounded local differences because
the current property contract does not expose saturation derivatives.

## Validity and interpretation

The inlet enthalpy must lie inside the liquid-vapor saturation dome at separator pressure. A
subcooled or superheated inlet is rejected with a recoverable physical diagnostic instead of
producing a negative phase flow. A separate heater, cooler, throttle, or flash process can bring a
stream to an admissible state before separation.

This model assumes instantaneous thermodynamic equilibrium, perfect phase disengagement, no heat
loss, and no stored inventory. It does not model entrainment, liquid carryover, residence time,
level control, pressure dynamics, nonequilibrium flashing, or separator sizing. Those inventory
effects belong to the separate registered
[dynamic equilibrium drum](dynamic-equilibrium-drum.md), which shares the saturation-property
foundation without changing this steady separator contract.
