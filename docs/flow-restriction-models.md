# Flow restriction models

Thermox exposes two calculation models under the physical template
`restriction.fluid.orifice`:

- `restriction.fluid.orifice.nonflashing_liquid`;
- `restriction.fluid.orifice.perfect_gas`.

Both are unidirectional, steady, adiabatic restrictions with an inlet and outlet fluid port. They
conserve mass and use isenthalpic downstream closure. The physical area is derived from the
declared `flow_diameter`; `discharge_coefficient` represents contraction and irreversible losses.
The coefficient must come from suitable geometry data, a correlation, or calibration—it is not an
OEM-independent constant supplied by the platform.

## Non-flashing liquid

For upstream density `rho`, effective area `C_d A`, and pressure drop `delta_p`, the model solves

```text
m_dot = C_d A sqrt(2 rho delta_p)
h_out = h_in
```

The selected property backend must support PH state evaluation and saturation at pressure. The
model requires a liquid inlet and checks the isenthalpic inlet value against saturated-liquid
enthalpy at outlet pressure. If that boundary is reached, evaluation stops with a recoverable
diagnostic. It does not silently extrapolate the incompressible formula into cavitating or flashing
flow.

## Perfect-gas choking

The gas model obtains inlet density and the local heat-capacity ratio `gamma = cp/cv` from the
registered property package. With downstream/upstream pressure ratio `r`, it uses

```text
r_critical = (2 / (gamma + 1))^(gamma / (gamma - 1))
r_effective = max(r, r_critical)

(m_dot / (C_d A))^2 =
    2 gamma rho_in p_in / (gamma - 1)
    * (r_effective^(2/gamma) - r_effective^((gamma+1)/gamma))
```

Flow therefore increases as downstream pressure falls until the critical ratio is reached, then
remains choked. The mass-flux curve is continuously differentiable at the ideal critical point, so
the existing sparse Newton kernel handles regime entry without a complementarity variable.

## Validity boundaries

These are deliberately separate calculation models rather than switches on a generic valve:

- the liquid model excludes cavitation, flashing, and two-phase discharge;
- the gas model assumes a locally calorically perfect gas and a quasi-steady isentropic approach
  to the vena contracta;
- neither model includes valve travel, actuator dynamics, Reynolds-dependent discharge
  coefficients, piping recovery factors, IEC/ISA sizing factors, or acoustic limits;
- reverse flow is rejected; a bidirectional network model needs explicit upstream selection and a
  differentiable flow-reversal formulation.

A credible flashing-flow model needs a selected engineering standard or validated homogeneous/
nonequilibrium correlation and its required geometry coefficients. That remains a distinct future
component model. This boundary is intentional: a solver convergence result must not be mistaken for
validated two-phase valve physics.
