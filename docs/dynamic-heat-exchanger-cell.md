# Dynamic heat-exchanger cell

`heat_exchanger.fluid.dynamic_cell` is a cycle-independent, two-fluid heat-transfer component with
stored hot-fluid energy, stored cold-fluid energy, and wall/metal thermal capacitance. The same
registered kind has a steady algebraic form and a transient index-1 DAE form.

The component has `hot_in`, `hot_out`, `cold_in`, and `cold_out` fluid ports. Each side may bind a
different registered property package. This allows gas-to-water, water-to-water, refrigerant, CO₂,
and other combinations without changing the component equations.

`hot_inventory` and `cold_inventory` accounting ports expose the two declared holdup masses in
both steady and transient graphs. They can feed system inventory reporting or a total-charge
constraint in a steady graph, but do not transport fluid or add a second pressure/enthalpy state.

## Physical model

Each side is a well-mixed, constant-holdup control volume. For fluid mass `M`, stored internal
energy `U`, inlet/outlet mass flow `m_in` and `m_out`, inlet enthalpy `h_in`, mixed enthalpy `h`,
and fluid-to-wall heat flow `Q`, the transient equations are

```text
m_out = m_in
dU/dt = m_in h_in - m_out h - Q
U = M u(p, h)
```

The hot and cold heat flows and wall balance are

```text
Q_hot  = UA_hot  (T_hot - T_wall)
Q_cold = UA_cold (T_wall - T_cold)

C_wall dT_wall/dt = Q_hot - Q_cold
```

Temperatures, density, and internal energy come from the media's PH property packages. The PH
derivative contract supplies the fixed sparse DAE Jacobian, using the platform's bounded derivative
fallback when a backend does not provide analytic derivatives.

Each side applies a quasi-steady local-loss relation at its outlet:

```text
p_in - p_out = K m |m| / (2 rho A²)
A = pi D² / 4
```

`hot_flow_diameter`, `cold_flow_diameter`, and their loss coefficients therefore describe the
cell's flow passage. More detailed distributed friction or external fittings can be represented by
separate registered transport components.

## Steady limit and multi-cell composition

At zero stored-energy derivatives, hot duty equals cold duty. Eliminating wall temperature gives
the mixed-cell conductance

```text
UA_effective = UA_hot UA_cold / (UA_hot + UA_cold)
Q = UA_effective (T_hot - T_cold)
```

This is intentionally a well-mixed cell, not an LMTD model of an entire exchanger. Multiple cells
can be connected in series. Connecting the cold path in the opposite order creates a counterflow
discretization; connecting it in the same order creates a co-current discretization. Economizers,
evaporators, superheaters, reheaters, recuperators, and reactor heat-transport sections should be
declared as graphs of generic cells rather than introduced as cycle-specific solver code.

The runnable [two-cell counterflow reference](distributed-heat-exchanger.md) demonstrates this
ordering with boundary components and verifies both the steady and transient graph.

## Geometry-closed steady holdup

`heat_exchanger.fluid.steady_finite_volume_cell` implements the same mixed-cell steady heat duty
and pressure-loss equations, but replaces prescribed hot/cold masses with `hot_fluid_volume` and
`cold_fluid_volume`. Each inventory output is closed at the mixed outlet state:

```text
M_hot  = rho_hot(p_hot,out, h_hot,out) V_hot
M_cold = rho_cold(p_cold,out, h_cold,out) V_cold
```

The two inventory ports retain distinct medium identities. A system may therefore include only the
working-fluid side in a refrigerant charge constraint while independently reporting the utility
side. Multiple finite-volume cells form a distributed steady exchanger whose total holdup changes
with pressure, enthalpy, phase, and the selected property package.

This model is steady-only. A faithful variable-mass transient exchanger requires differential mass
and internal-energy balances on each cell; using an algebraic steady charge closure inside the
existing constant-holdup DAE would be physically and numerically incorrect.

## Run the reference

The gas-to-water example binds ideal-gas air on the hot side and IF97 water on the cold side:

```sh
./build/thermox_cli simulate \
  --model core/examples/dynamic_heat_exchanger_cell.json \
  --case heat_up \
  --end-time 0.5 \
  --format json
```

The regression checks consistent initialization, adaptive integration, cross-property-package
operation, positive hot-to-cold transfer, and exact total stored-energy-rate closure against the
boundary enthalpy flows.

## Scope and limits

The constant-holdup masses are equipment parameters, so connecting only these cells to a fixed
total-charge constraint is redundant; at least one variable-mass, geometry-closed inventory is
needed for charge to determine pressure. The constant-holdup approximation neglects fluid mass accumulation, compressible pressure-wave
dynamics, axial conduction, heat loss to ambient, radiation, fouling evolution, flow reversal, and
finite transport delay. It is suitable as the first index-1 thermal cell and as a building block for
spatial discretization. Variable-mass vessels and two-phase level dynamics belong to the registered
rigid-volume and equilibrium-drum components. A future phase-change cell may add void-fraction and
moving-boundary correlations as a separate calculation model under the same physical template.
