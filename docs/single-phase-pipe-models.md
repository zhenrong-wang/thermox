# Single-phase pipe models

Thermox provides two calculation models under the physical template `pipe.fluid`:

- `pipe.fluid.darcy_weisbach` is adiabatic;
- `pipe.fluid.darcy_weisbach_heat_transfer` adds an explicit `ambient` heat port.

Both are ordinary registered graph components. They are not tied to a cycle, plant type, or fluid.
The selected property package must provide `state_ph` and `transport`; the compiler rejects an
incompatible medium before solving.

## Hydraulic closure

The model conserves mass and evaluates the fluid at the mean endpoint pressure and enthalpy. For
pipe area `A`, diameter `D`, length `L`, roughness `epsilon`, elevation change `delta_z`, and local
loss coefficient `K`, it closes pressure with

```text
Re = |m_dot| D / (A mu)
p_in - p_out = (f L/D + K) m_dot |m_dot| / (2 rho A^2)
                 + rho g delta_z
```

`delta_z` is outlet elevation minus inlet elevation, so a positive value is an upward run. The
Darcy friction factor uses `64/Re` below Reynolds number 2300 and the Haaland approximation above
4000. A smoothstep blend avoids a discontinuity in the transition region. Reverse flow preserves
the sign of the friction term. The zero-flow limit is defined as zero friction loss.

The pressure row has a declared sparse pattern. Its local derivatives are calculated by bounded
central differences because the property contract does not currently expose viscosity derivatives;
one-sided differences are used close to a property boundary. This is an isolated implementation
choice behind the component contract and can later be replaced by analytic or automatic
differentiation without changing model documents.

## Energy closure

The adiabatic model uses `h_out = h_in`. The heat-transfer model solves

```text
m_dot (h_out - h_in) + Q_ambient = 0
Q_ambient = UA ((T_in + T_out)/2 - T_ambient)
```

Positive `Q_ambient` means heat rejected by the fluid. Keeping this duty on a heat connector makes
the external thermal boundary visible to topology, result projection, and system energy audits.
The model therefore does not hide ambient heat loss inside an unreported component parameter.

## Scope and hard limits

This is a steady, quasi-one-dimensional, homogeneous single-phase model. It deliberately rejects a
two-phase mean state. It does not model choking, Fanno-flow compressibility, flashing, condensation,
distributed wall storage, finite-volume wave propagation, or phase slip. Those behaviors require
separate registered models rather than case-specific switches in this one. Elevation appears in the
pressure closure; kinetic and potential energy are not added to the current fluid connector's
enthalpy audit.
