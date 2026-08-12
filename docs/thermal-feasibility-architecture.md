# Thermal-feasibility audit

Convergence and energy conservation are necessary but not sufficient for a physically admissible
heat-exchanger result. A prescribed-duty model can close its equations while implying a terminal
temperature cross. Thermox therefore exposes the transport-neutral
`thermox.thermal_feasibility/v1` result contract separately from nonlinear diagnostics and
validation-evidence strength.

For every result component with `hot_in`, `hot_out`, `cold_in`, and `cold_out` ports, the generic
counterflow audit evaluates:

```text
delta_T_a = T_hot,in  - T_cold,out
delta_T_b = T_hot,out - T_cold,in
minimum_approach = min(delta_T_a, delta_T_b)
```

The component passes when `minimum_approach` is at least the caller's finite, nonnegative required
minimum. The summary contains every component result, aggregate counts, and the required threshold;
its validator rejects non-finite values, duplicate component identities, inconsistent derived
minima, verdicts, or aggregate counts. Every successful steady and transient result now carries the
stable summary. Transient evidence retains the worst approach and its sample time for each
component across the complete stored trajectory.

Each audited result graph also exposes `counterflow_hot_in_minus_cold_out`,
`counterflow_hot_out_minus_cold_in`, and `counterflow_minimum_approach` component metrics. Their
dimension is `temperature_difference`, whose canonical `delta_K` and engineering `delta_degC`
units deliberately have no absolute-temperature offset. Normal Study projections and acceptance
criteria can therefore govern physical admissibility without a heat-exchanger-specific job path.
The web result workspace presents the physical verdict separately from numerical convergence.

The audit is component-kind agnostic: it recognizes the physical port contract, so registered and
future user-provided exchanger implementations receive the same check. UA-based counterflow models
already reject nonpositive terminal differences during equation evaluation. The audit closes the
gap for fixed-duty, energy-balance, calibrated, and externally implemented models whose equations
do not themselves determine or constrain a positive approach.

The NETL B31A segmented HRSG intentionally exercises the distinction. Its unpublished assumed
surface order solves and conserves mass/energy, but the audit identifies negative terminal
approaches. Thermox therefore treats that graph as a topology and boundary-decomposition test—not
as a calculation-ready coil design or predictive HRSG validation.
