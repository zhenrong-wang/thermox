# Semi-physical volumetric expander

`expander.fluid.semi_physical_volumetric` is a property-backed, parameter-driven model for
scroll-like positive-displacement expanders. It is intended for component calibration and
off-design system studies when machine geometry and loss parameters are available but a chamber-
resolved deterministic model is not justified.

The model is medium-agnostic. It obtains every thermodynamic state from the property package bound
to the component's fluid ports and therefore does not embed R245fa or any ORC-specific constants.

## Physical decomposition

For inlet density `rho_in`, maximum chamber volume per revolution `V_max`, built-in volume ratio
`r_v`, and rotational speed `N` in revolutions per second, the trapped chamber flow is

```text
m_internal = rho_in * V_max / r_v * N
```

The built-in discharge state is found from an isentropic constant-mass expansion to
`rho_in / r_v`. Thermox solves this state with the active real-fluid property package rather than
assuming an ideal-gas exponent. The indicated power includes both trapped expansion and the
constant-volume pressure-equalization work:

```text
W_indicated = m_internal * [h_in - h_built_in
                            + (p_built_in - p_out) / rho_built_in]
```

This term naturally represents under-expansion or over-expansion relative to the imposed outlet
pressure. A fictitious isentropic nozzle supplies the parallel leakage flow. The nozzle is choked
when its critical pressure is above the imposed outlet pressure, and its throat state comes from
the active property package.

Mechanical loss follows the common semi-analytical form of a proportional indicated-power term
plus a speed-squared term represented at a declared reference speed:

```text
W_mechanical_loss = fraction * W_indicated
                  + W_loss,reference * (omega / omega_reference)^2
W_shaft = W_indicated - W_mechanical_loss
```

Ambient heat loss uses an overall conductance and the mean of inlet and built-in chamber
temperatures as a reduced wall-temperature approximation. The outlet enthalpy and explicit
rejected-heat port close the external component energy balance exactly:

```text
m_total * (h_in - h_out) = W_shaft + Q_rejected
```

## Parameters

- `maximum_chamber_volume_per_revolution`: chamber volume at the end of built-in expansion.
- `built_in_volume_ratio`: maximum chamber volume divided by intake-closure volume; must exceed 1.
- `leakage_area`: lumped fictitious nozzle area; zero disables leakage.
- `leakage_discharge_coefficient`: nozzle discharge coefficient in `[0, 1]`.
- `mechanical_loss_at_reference_speed`: speed-dependent loss power at the declared reference speed.
- `mechanical_loss_reference_angular_speed`: reference angular speed for the speed-squared loss.
- `proportional_mechanical_loss`: fraction of indicated power dissipated mechanically.
- `ambient_heat_transfer_conductance`: reduced wall-to-ambient conductance.
- `ambient_temperature`: ambient heat-sink temperature, also exposed on the rejected-heat port.

The result contract also exposes `built_in_pressure`, `internal_mass_flow`, `leakage_mass_flow`,
`indicated_power`, `mechanical_loss_power`, and `ambient_heat_loss` as solved internal variables.
These diagnostics make calibration residuals and loss attribution auditable without changing the
physical port topology.

## Scope and claim limit

The component supports steady and quasi-steady transient graphs. It is not a chamber-resolved
transient model and currently omits suction/exhaust port pressure losses, clearance/recompression,
oil effects, and a solved wall thermal state. Those effects require a richer component kind rather
than hidden case-specific corrections.

The formulation follows the physical decomposition reported by Oudkerk, Dickes, and Lemort,
"Development of a semi-analytical model of volumetric expander for system-level simulation"
(ECOS 2016), and the leakage/heat/mechanical-loss structure established by Lemort et al.,
"Testing and modeling a scroll expander integrated into an Organic Rankine Cycle," Applied
Thermal Engineering 29 (2009), DOI 10.1016/j.applthermaleng.2009.04.013.

Synthetic regression proves equation assembly, real-fluid state evaluation, domain handling, and
mass/energy conservation. It is not hardware validation. Calibrated parameters require machine
geometry and training data, and predictive claims require an independent operating envelope.
