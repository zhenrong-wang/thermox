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

## Geometry-closed steady and transient holdup

`heat_exchanger.fluid.finite_volume_cell` implements the same mixed-cell steady heat duty
and pressure-loss equations, but replaces prescribed hot/cold masses with `hot_fluid_volume` and
`cold_fluid_volume`. In steady operation each inventory output is closed at the mixed bulk state:

```text
M_hot  = rho_hot(p_hot,bulk, h_hot,bulk) V_hot
M_cold = rho_cold(p_cold,bulk, h_cold,bulk) V_cold
```

The two inventory ports retain distinct medium identities. A system may therefore include only the
working-fluid side in a refrigerant charge constraint while independently reporting the utility
side. Multiple finite-volume cells form a distributed exchanger whose total holdup changes with
pressure, enthalpy, phase, and the selected property package.

In transient operation, each side owns differential mass and total-internal-energy states:

```text
dM/dt = m_dot,in - m_dot,out
dU/dt = m_dot,in h_in - m_dot,out h_bulk - Q_dot,to_wall
M      = rho(p_bulk, h_bulk) V
U      = M u(p_bulk, h_bulk)
```

The wall temperature remains a separate differential state. The formulation permits filling,
draining, compression, and thermal expansion while retaining exact mass and total-energy
accounting. A flow/enthalpy inlet paired with a pressure outlet is one valid causal boundary set;
fixing both inlet and outlet pressure in addition to inlet flow over-specifies a side.

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
operation, nonzero mass accumulation, geometry closure, positive hot-to-cold transfer, and exact
total stored-energy-rate closure against the boundary enthalpy flows.

The regime-spanning reference starts its closed water side inside the two-phase region and transfers
energy from a closed ideal-gas side through the wall until the water becomes vapor:

```sh
./build/thermox_cli simulate \
  --model core/examples/regime_spanning_finite_volume_heat_exchanger.json \
  --case boil_to_vapor \
  --end-time 10 \
  --format json
```

It crosses the saturation boundary without a model switch or rejected integration step while
conserving each side's mass, total fluid-plus-wall energy, and both geometric volume closures.

## Correlation-driven conductance

`heat_exchanger.fluid.finite_volume_correlated_cell` retains the same ports, geometric inventory,
mass/energy balances, wall state, and pressure-loss parameters. Instead of fixed `hot_side_UA` and
`cold_side_UA`, each component instance must bind two independent `thermox.correlation` artifacts:

- `hot_side_conductance_correlation`;
- `cold_side_conductance_correlation`.

Both artifacts must output `thermal_conductance` with dimension `thermal_conductance` (W/K). An
artifact declares only the local inputs it uses. The model supplies dimension-checked pressure,
enthalpy, temperature, density, vapor quality, heat capacity, transport properties, mass flow,
mass flux, Reynolds and Prandtl numbers, diameter, area, and—when requested—saturated liquid/vapor
density and latent heat. In transient compilation it also supplies `wall_temperature` and the
signed `wall_to_fluid_temperature_difference = T_wall - T_fluid`. This permits conductance laws
that depend on wall superheat or subcooling without duplicating the wall balance inside an
artifact. Requesting transport or saturation inputs requires the bound property package to
advertise the corresponding capability.

Wall-independent artifacts work in both steady and transient compilation. The steady formulation
eliminates wall temperature analytically, so an artifact requesting either wall input is rejected
for steady compilation rather than being evaluated with an invented wall state. A future explicit
steady wall formulation can relax that boundary without changing the artifact contract.

Correlation applicability envelopes and candidate selection remain owned by the normal artifact
layer. Consequently, a user can bind different single-phase, boiling, or condensation conductance
families to component instances without changing the exchanger equations or numerical kernel.
Invalid output dimensions, unsupported inputs, missing property capabilities, and nonpositive
conductance are rejected explicitly.

## Scope and limits

The older `heat_exchanger.fluid.dynamic_cell` keeps constant masses as equipment parameters and is
useful when pressure inventory dynamics are intentionally outside the model boundary. The
finite-volume model is the appropriate cell when charge must determine pressure dynamically. Both
approximations neglect compressible pressure-wave propagation, axial conduction, heat loss to
ambient, radiation, fouling evolution, flow reversal, and finite transport delay. A future
specialized cell may add slip-dependent void fraction, moving boundaries, boiling/condensation heat
transfer, dryout, and critical-flow correlations under the same physical template. Those closures
must be selected explicitly; they are not implied by successful homogeneous-equilibrium traversal
of a saturation boundary. The conductance contract now exposes transient wall superheat, but does
not itself provide a nucleate-boiling, condensation, dryout, or critical-heat-flux law. Those remain
versioned engineering artifacts whose applicability and validation evidence must be supplied.
