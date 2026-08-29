# Correlation-driven volumetric expander

`expander.fluid.volumetric_correlations` is a generic reduced-order model for scroll, screw,
reciprocating, and other positive-displacement expanders. It does not encode an ORC working fluid,
machine geometry, manufacturer, or benchmark-specific coefficient.

The component exposes fluid `inlet`/`outlet`, shaft-power output, and rejected-heat output ports. It
requires a positive `displacement_per_revolution` and three independently versioned
`thermox.correlation/v2` artifacts:

- `filling_factor_correlation` -> dimensionless `filling_factor`;
- `fluid_efficiency_correlation` -> dimensionless `fluid_isentropic_efficiency`;
- `shaft_efficiency_correlation` -> dimensionless `shaft_isentropic_efficiency`.

Each artifact may request any subset of pressure ratio, pressure drop, inlet pressure, inlet
temperature, inlet density, and angular speed. Applicability ranges and Study operating envelopes
therefore remain artifact-owned engineering evidence.

## Equations

For displacement per revolution `V_d`, filling factor `phi`, inlet density `rho_in`, and angular
speed `omega`, the mass-capacity closure is

```text
m_dot = phi rho_in V_d omega / (2 pi)
```

The property package evaluates the isentropic outlet enthalpy `h_out,s` at measured or solved outlet
pressure and inlet entropy. The two efficiencies separately close the fluid state and useful shaft
power:

```text
h_out = h_in - eta_fluid (h_in - h_out,s)
W_shaft = m_dot eta_shaft (h_in - h_out,s)
```

The model requires `0 < eta_shaft <= eta_fluid <= 1`. The remaining fluid-energy reduction is
reported rather than discarded:

```text
Q_rejected = m_dot (h_in - h_out) - W_shaft
```

This exact balance lets a system connect the loss stream to an ambient model or retain it as an
explicit boundary result. The closure is steady and quasi-steady transient; it does not model
within-revolution chamber dynamics.

## Evidence boundary

The contract follows the empirical variables described in published scroll-expander literature,
which reports polynomial filling-factor/efficiency models using inlet pressure, pressure ratio, and
speed, as well as higher-fidelity leakage and wall-heat-transfer models. The component supports
those reduced-order closures without claiming that one universal correlation applies to every
machine.

The 1 kW R245fa benchmark can identify an effective volumetric capacity and two observed
efficiencies from its measured component boundaries. A physical filling factor cannot be separated
from displacement unless machine geometry is sourced. Any validation using an inferred or
secondary-source displacement must mark that quantity as assumption-dependent; only predicted mass
flow, outlet state, and shaft power may be treated as component-validation outputs.

Primary references:

- Oh et al., “Numerical and experimental investigation on thermal-hydraulic characteristics of a
  scroll expander for organic Rankine cycle,” *Applied Energy* 278 (2020) 115672,
  https://doi.org/10.1016/j.apenergy.2020.115672.
- Oh et al., “Development of a fully deterministic simulation model for organic Rankine cycle
  operating under off-design conditions,” *Applied Energy* 307 (2022) 118149,
  https://doi.org/10.1016/j.apenergy.2021.118149.
