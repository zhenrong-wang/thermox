# Dynamic equilibrium drum

`drum.fluid.equilibrium_two_phase` is the inventory-bearing dynamic counterpart to the steady
equilibrium flash separator. It represents a rigid, uniform-cross-section vessel containing
saturated liquid and vapor in instantaneous thermodynamic equilibrium.

The component is transient-only. Its typed ports are:

- one fluid inlet;
- saturated-vapor and saturated-liquid outlets;
- one heat inlet, with positive `Q_dot` adding energy to the drum;
- one normalized level-signal output, equal to `liquid_level / vessel_height`.
- one inventory-accounting output carrying the live total mass.

All fluid ports use the same registered medium. The property backend must provide saturation states
at pressure.

## Differential balances

Total stored mass `M` and total internal energy `U` are differential states:

```text
dM/dt = m_dot_in - m_dot_vapor - m_dot_liquid

dU/dt = m_dot_in h_in
        - m_dot_vapor h_g
        - m_dot_liquid h_f
        + Q_dot
```

Using enthalpy for transported energy and internal energy for stored energy is the control-volume
first law for a rigid vessel. The component does not conflate the two quantities.

## Equilibrium and rigid-volume closure

Pressure `p`, vapor mass quality `x`, and liquid level `L` are algebraic states. At pressure `p`,
the property backend supplies saturated densities `rho_f`, `rho_g`, internal energies `u_f`, `u_g`,
enthalpies `h_f`, `h_g`, and temperature `T_sat`:

```text
V = M ((1 - x) / rho_f + x / rho_g)
U = M ((1 - x) u_f + x u_g)

p_vapor_out = p_liquid_out = p
p_inlet = p
h_vapor_out = h_g
h_liquid_out = h_f
T_heat_port = T_sat
```

For the current uniform-cross-section geometry approximation,

```text
L = vessel_height / V * M (1 - x) / rho_f
```

The reported level is therefore a geometric consequence of liquid phase volume, not a separate
calibration variable. The normalized level signal allows the drum to connect to the platform's
dimensionless control blocks without exposing an internal implementation variable. Equating inlet
port pressure to vessel pressure also gives an upstream feed valve the downstream pressure needed
to solve its flow law.

The inventory output is similarly narrow: it exposes conserved mass for total-charge accounting
but carries no flow, pressure, enthalpy, phase, or level semantics. A drum, receiver, or accumulator
can therefore contribute to a system constraint without coupling that constraint to its private
equilibrium formulation.

## Initialization

Dynamic cases should provide physically consistent initial guesses for:

- `total_mass`;
- `total_internal_energy`;
- `pressure`;
- `vapor_quality`;
- `liquid_level`.

The DAE consistent-initialization pass holds the differential inventory states and solves the
algebraic equilibrium states and state derivatives. A convenient initialization from chosen
pressure and quality is:

```text
M = V / ((1 - x) / rho_f + x / rho_g)
U = M ((1 - x) u_f + x u_g)
```

## Scope and limitations

The model assumes a rigid vessel, uniform pressure and temperature, instantaneous phase
equilibrium, perfect phase-specific outlets, and a constant cross-sectional area. It does not yet
include entrainment, carryover, shrink/swell, hydrostatic pressure gradients, wall thermal storage,
non-condensable gases, dissolved species, relief-valve flow, or actuator/control dynamics.

Saturation-property derivatives are evaluated by bounded local pressure differences because the
property contract does not currently expose saturation derivatives. The DAE equations retain a
fixed sparse pattern, so a future analytic derivative capability can replace that local calculation
without changing model documents or component topology.
