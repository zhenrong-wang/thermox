# Dynamic component primitives

Thermox provides reusable dynamic equipment primitives through the same registered component and
connector contracts used by steady models. They are not tied to a particular thermal cycle.

## Two-sided wall thermal storage

`storage.thermal.wall_two_sided` has `hot_side` and `cold_side` heat ports plus a differential
wall-temperature state. Its transient equations are:

```text
T_hot = T_wall
T_cold = T_wall
C dT_wall/dt = Q_hot - Q_cold
```

`thermal_capacity` is strictly positive and expressed in `J/K`. The steady form removes
accumulation and enforces equal surface temperatures and heat rates. Heat-transfer coefficients,
fluid films, radiation, and spatial wall discretization belong in connected components or more
detailed future wall models; this primitive represents only lumped stored energy.

## Two-port rotating inertia

`shaft.inertia.two_port` connects one driver and one load through the shaft domain. It stores
rotational kinetic energy rather than dividing power by angular speed:

```text
E_rot = 1/2 J omega²
dE_rot/dt = efficiency P_driver - P_load - fixed_loss
```

Both shaft-port speeds equal the internal algebraic speed. The energy formulation is well behaved
for low speeds and keeps the existing power/speed connector contract unchanged. Cases should set
`rotor.rotational_energy` when a specific initial speed is required; for inertia `J` and initial
speed `omega_0`, use `0.5 J omega_0²`. A model imposing finite power at exactly zero speed should
normally provide a physically compatible startup law; a future torque-aware connector can model
zero-speed torque directly.

`moment_of_inertia` is registered canonically as `kg*m2`, with `kg*m^2` accepted as an alias.
Mechanical efficiency defaults to one and fixed loss defaults to zero. The steady form reduces to
speed continuity and the corresponding power balance.

## Multi-load rotating inertia

`shaft.inertia.two_load` extends the same kinetic-energy formulation to one driver and two
common-speed loads. `shaft.inertia.geared_two_load` provides one direct load plus one fixed-ratio
geared load. The geared model declares the ratio as driver speed divided by geared-load speed and
accounts for gearbox efficiency in both its steady balance and transient energy accumulation.

These are dynamic counterparts to `shaft.train.two_load` and
`shaft.train.geared_two_load`; they are generic topology primitives, not gas-turbine-specific
components. They allow a rotor to couple a turbine, compressor, generator, accessory, or any other
registered shaft participant without collapsing those machines into one case-specific equation.
Each stores `rotational_energy` as its differential state and exposes internal algebraic `omega`.
As with the two-port model, initialize a desired shaft speed with `0.5 J omega_0²`.

## Normalized control blocks

The first control primitives deliberately operate on the existing dimensionless signal/control
contracts:

- `control.proportional.normalized` maps a signal measurement to a control command using
  `command = gain * measurement + bias`.
- `control.first_order_lag.normalized` maps a control command to a control response using
  `tau d(response)/dt + response = gain * command`.
- `control.pi_bounded.normalized` accepts explicit setpoint and measurement signals, bounds its
  command, and tracks saturation through a differential integral state with back-calculation
  anti-windup.

The lag's steady form is the zero-derivative relation. Its `time_constant` is strictly positive and
uses the registry-owned time dimension. Signal, control, and shaft source/sink boundaries make
these paths independently composable and testable.

These blocks assume their values have already been normalized. Dimensioned measurements, rate
limits, dead bands, derivative filtering, and actuator-specific behavior should remain explicit
registered models rather than being hidden inside unrelated equipment equations.
