# Actuated valve control

`valve.fluid.actuated_nonflashing_liquid` is a generic steady/transient valve model with fluid inlet
and outlet ports plus a normalized control input. It provides the equipment-side bridge between
the platform's signal/control graph and a physical flow equation.

The model parameters are:

- `full_open_diameter`;
- `discharge_coefficient`;
- optional `minimum_opening`, defaulting to zero.

For normalized command `c` in `[0, 1]`, the effective opening and mass flow are

```text
opening = minimum_opening + (1 - minimum_opening) c
A_effective = C_d pi D^2 / 4 * opening
m_dot = A_effective sqrt(2 rho_in (p_in - p_out))
h_out = h_in
```

The command changes effective area rather than overwriting mass flow. Flow therefore remains a
solution of the connected hydraulic graph and responds to both actuator position and boundary
pressures.

## Dynamic composition

The valve's fluid relation is algebraic in both steady and transient modes. Actuator dynamics are
composed explicitly using `control.first_order_lag.normalized`:

```text
controller command -> first-order actuator lag -> valve.command
```

This keeps actuator time constants out of the valve's hydraulic correlation and allows different
actuators to be attached to the same valve model. The dynamic graph compiler and DAE kernel preserve
the control response as a differential state while solving valve flow algebraically at each step.

The equilibrium drum publishes `level_signal = liquid_level / vessel_height`. It can feed the
registered [bounded PI controller](bounded-pi-control.md), whose output can pass through the
actuator lag into this valve. Rate limiting, dead band, derivative action, and fail-position
behavior remain explicit future control blocks.

## Physical boundaries

This model is restricted to unidirectional, non-flashing liquid service. It verifies a liquid inlet
and checks inlet enthalpy against saturated-liquid enthalpy at outlet pressure. It rejects flashing
conditions rather than extrapolating the liquid square-root law.

The linear command-to-area characteristic is a declared model assumption. Equal-percentage,
quick-opening, Reynolds-corrected, IEC/ISA sizing, cavitation, and flashing behavior require
separate models or bound engineering-characteristic artifacts. The discharge coefficient and
effective diameter must come from appropriate equipment data or calibration.
