# Flow restriction models

Thermox exposes three distinct restriction calculation models:

- `restriction.fluid.orifice.nonflashing_liquid`;
- `restriction.fluid.orifice.perfect_gas`;
- `restriction.fluid.local_loss.homogeneous_two_phase`.

All are unidirectional, adiabatic restrictions with inlet and outlet fluid ports. They conserve
mass and use isenthalpic transport. The two orifice models are steady calculation models whose
`discharge_coefficient` represents contraction and irreversible losses. The homogeneous two-phase
model supports steady and transient graphs and uses a dimensionless local `loss_coefficient`.
Coefficients must come from suitable geometry data, correlations, or calibration; the platform
does not invent equipment-independent values.

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

## Homogeneous two-phase local loss

`restriction.fluid.local_loss.homogeneous_two_phase` supplies the hydraulic impedance needed
between two independently stored saturated-mixture volumes. At the mean pressure and transported
enthalpy, the registered property backend supplies homogeneous-equilibrium mixture density
`rho_mix`. For flow area `A`, loss coefficient `K`, and positive forward mass flow, the model solves

```text
p_in - p_out = K m_dot |m_dot| / (2 rho_mix A^2)
h_out = h_in
```

Both inlet and outlet pressures participate in the mean property state, and bounded numerical
derivatives retain a fixed sparse Jacobian pattern. The model rejects non-positive pressure drop,
reverse flow, and a mean state outside the two-phase region. This explicit impedance is important
for DAE topology: directly joining two rigid equilibrium inventories with an ideal pressure link
forces two independently determined inventory pressures to be identical and can create a
dependent initialization constraint.

## Validity boundaries

These are deliberately separate calculation models rather than switches on a generic valve:

- the liquid model excludes cavitation, flashing, and two-phase discharge;
- the gas model assumes a locally calorically perfect gas and a quasi-steady isentropic approach
  to the vena contracta;
- the homogeneous model assumes zero phase slip and thermodynamic equilibrium, and it does not
  predict flashing delay, critical two-phase flow, or choking;
- none of the models includes valve travel, actuator dynamics, Reynolds-dependent discharge
  coefficients, piping recovery factors, IEC/ISA sizing factors, or acoustic limits;
- reverse flow is rejected; a bidirectional network model needs explicit upstream selection and a
  differentiable flow-reversal formulation.

A predictive relief valve, flashing orifice, or choking two-phase nozzle needs a selected
engineering standard or validated homogeneous/nonequilibrium correlation and its required geometry
coefficients. That remains a distinct component model. This boundary is intentional: a solver
convergence result must not be mistaken for validated two-phase valve physics.
