# Thermox Schema Design Draft

_Date: 2026-07-27_

This document defines the current interchange concepts for the thermal balance calculation
platform. The project is in early development: the schema is intentionally allowed to change
without backward-compatibility adapters while the architecture is established.

Implementation status: the first-class `thermox_platform` C++ module parses and validates the
`thermox.model/v1` boundary for media, components, typed ports, connections, cases, and scalar unit
normalization. Its metadata-backed component registry and graph compiler validate port contracts
and property capabilities, create canonical port variables, and lower connections, fixed values,
component equations, internal dynamic states, and accumulation equations into sparse steady or DAE
assembly metadata. Design-only metadata such as
annotations/options remains documented here for registry/frontend use and may be ignored by the
current parser.

## 1. Goals

- Represent arbitrary thermal/fluid system topology as typed components, ports, and connections.
- Separate model topology from operating cases and solver options.
- Support design and off-design calculations.
- Preserve enough metadata for units, validation, equation generation, diagnostics, and reproducibility.
- Keep the schema generic for combined cycle, diesel heat recovery, nuclear steam cycles, refrigeration, and future dynamic models.

## 2. Top-level model document

```yaml
schema_version: thermox.model/v1
model:
  id: ccgt_demo
  name: Combined Cycle Demo
  revision: rev_001
  media: []
  components: []
  connections: []
  annotations: {}
cases: []
```

## 3. Medium package

```yaml
id: water_steam
backend: coolprop_if97
substance: Water
options:
  reference_state: IAPWS_IF97
  valid_region_policy: error
```

Recommended backend IDs:

- `coolprop_if97`: water/steam IF97 style calculations.
- `coolprop_heos`: general pure fluids and refrigerants.
- `ideal_gas_mixture`: lightweight gas model for early Brayton/HRSG work.
- `cantera_solution`: combustion/reacting gas mixtures.
- `refprop`: optional high-accuracy licensed backend adapter.

## 4. Component instance

```yaml
id: st_hp
label: HP Steam Turbine
kind: turbine.steam.isentropic_efficiency
version: 1.0.0
ports:
  inlet:
    domain: fluid
    medium: water_steam
    direction: in
  outlet:
    domain: fluid
    medium: water_steam
    direction: out
  shaft:
    domain: shaft
    direction: out
parameters:
  eta_is:
    value: 0.88
    unit: dimensionless
  mechanical_efficiency:
    value: 0.99
    unit: dimensionless
options:
  pressure_drop_model: none
annotations:
  ui:
    x: 100
    y: 200
```

## 5. Port domains

### 5.1 Fluid domain

Required fields:

```yaml
domain: fluid
medium: water_steam
direction: in | out | bidirectional
```

Canonical solver variables:

- `m_dot` — kg/s; positive follows the declared port direction and negative represents reversal.
- `p` — Pa.
- `h` — J/kg.
- `composition` — mass or mole fractions for mixtures (future extension).

`T`, entropy, density, phase, and vapor quality are property-derived results, not redundant
connector unknowns. A normal directed `fluid_link` equates `m_dot`, `p`, and `h` at its two ends.
Pressure-loss, heat-transfer, mixing, and junction behavior belongs to registered components rather
than hidden connection semantics.

A steady case may nevertheless specify `component.port.T` in `fixed_values`. The compiler lowers
that engineering boundary condition into a property-backed PH equation; it does not create a
temperature unknown. This allows natural `(p,T)` boundary input while preserving a non-redundant
connector formulation.

### 5.2 Heat domain

Canonical variables:

- `Q_dot` — W.
- `T` — K.

### 5.3 Shaft domain

Canonical variables:

- `W_dot` — W.
- `omega` — rad/s where maps require speed.
- optional torque.

### 5.4 Signal/control domain

Signal ports do not create conservation equations automatically. They supply scalar/vector values to component equations.

## 6. Connection

```yaml
id: c_main_steam
from: hrsg_hp_superheater.outlet
to: st_hp.inlet
kind: fluid_link
parameters:
  pressure_loss:
    model: fixed_fraction
    value: 0.02
```

Connection kinds:

- `fluid_link`: normal directed fluid connection.
- `fluid_junction`: multiple ports with generated mass/species/energy balance.
- `heat_link`: heat transfer connection.
- `shaft_link`: mechanical power/speed connection.
- `signal_link`: control/equation signal.

## 7. Case document

```yaml
id: design_100pct
label: 100% design load
mode: steady_state_design
fixed_values:
  ambient.outlet.m_dot:
    value: 100
    unit: kg/s
  ambient.outlet.p:
    value: 101325
    unit: Pa
  ambient.outlet.T:
    value: 288.15
    unit: K
  generator.P_e:
    value: 450
    unit: MW
initial_guesses:
  st_hp.inlet.p:
    value: 120
    unit: bar
solver_options:
  nonlinear_solver: damped_newton
  tolerance: 1.0e-8
  max_iterations: 80
  scaling: auto
```

Case modes:

- `steady_state_design`
- `steady_state_off_design`
- `dynamic_initialization`
- `dynamic_transient`
- `parameter_sweep` (future)
- `optimization` (future)

## 8. Component type metadata schema
Component type definitions should be served to the frontend and compiler from the same registry.
The current C++ implementation has registered component models exposing kind/version, required port
names, domains, directions, fluid-property capabilities, supported simulation modes, and formal
parameter descriptors. Each parameter descriptor records its SI dimension, requiredness, optional
default, lower/upper bounds, and whether each bound is inclusive. The compiler rejects missing,
unknown, dimensionally incompatible, non-finite, and out-of-range values before equation assembly.
Plain numeric values remain implicit SI; unit-bearing values must match the declared dimension.

The compiler resolves model media
through the property registry and injects ideal-gas, CO2, or IF97 packages into property-aware
compressor, turbine, and pump equations. The default registry also contains enthalpy-flow mixer
and splitter models, an isenthalpic pressure-ratio valve, a two-stream fixed-duty heat exchanger,
a property-backed counterflow-UA heat exchanger, quality-target evaporator and condenser models,
and a rigid adiabatic fluid volume. Heat-exchanger sides may select different media, but each side
must be internally consistent. Phase-change components expose typed heat ports and require a
strictly interior outlet quality until explicit saturation-pair queries are available. Broader
parameter schemas and frontend display metadata remain future extensions.

`ComponentRegistry::descriptors()` returns a stable, kind-ordered snapshot suitable for validation
services and future component-palette generation. Optional model behavior reads defaults from this
same descriptor rather than duplicating them inside the equation implementation.

Default catalog composition uses independent boundary, storage, turbomachinery, and fluid-transport
registrars plus dedicated heat-transfer/phase-change and fluid-inventory registrars. These modules
share a small internal support layer for compiled-variable lookup, medium-package lookup, required
parameter access, and property-failure translation. The graph compiler depends only on the public
`ComponentModel` contract and is unaffected by which registrar owns an implementation.

`heat_exchanger.fluid.counterflow_ua` uses counterflow terminal temperature differences obtained
from PH flashes and enforces `Q_dot = UA * LMTD`. `UA` accepts `W/K`, `kW/K`, or `MW/K`. A
temperature crossover is a recoverable model-evaluation failure during Newton line search.

`evaporator.fluid.fixed_outlet_quality` and `condenser.fluid.fixed_outlet_quality` conserve mass,
apply an optional fractional pressure loss, solve outlet quality through PH, and expose positive
heat-duty magnitude and phase-change temperature on an `in` or `out` heat port respectively.

Transient-capable component descriptors additionally declare:

- whether steady and/or transient compilation is supported;
- canonical port variables that become differential states;
- internal differential or algebraic variables, including initial values, bounds, and state and
  derivative scales;
- transient residual and sparse Jacobian assembly.

Cases use `initial_guesses` to initialize differential states. A transient case cannot place a
differential state in `fixed_values`, because that would constrain it for the entire trajectory
rather than initialize it.

`volume.fluid.rigid_adiabatic` is transient-only. It stores `mass` and `total_energy` as
differential states and uses algebraic `pressure` and `enthalpy` with PH density/internal-energy
closures. Its `volume` parameter accepts `m3`, `m^3`, or `L`; initial mass and total energy accept
`kg` and `J`/`kJ`/`MJ`.

Before producing a solver problem, each compiler compares the number of unknowns and residual
equations. Under- and over-specified graphs are rejected with model-level counts. Fixed sparse
patterns additionally enable structural matching in the numerical core.

```yaml
kind: turbine.steam.isentropic_efficiency
version: 1.0.0
display_name: Steam Turbine
category: steam_cycle
ports:
  - name: inlet
    domain: fluid
    direction: in
    medium_role: working_fluid
  - name: outlet
    domain: fluid
    direction: out
    medium_role: working_fluid
  - name: shaft
    domain: shaft
    direction: out
parameters:
  - name: eta_is
    unit: dimensionless
    required: true
    bounds: [0.0, 1.0]
    default: 0.88
variables:
  - name: inlet.p
    unit: Pa
    scale_hint: 1.0e6
residuals:
  count: dynamic
  differentiability: smooth_except_phase_boundary
```

## 9. Result schema

```yaml
simulation_id: sim_001
model_revision: rev_001
case_id: design_100pct
status: converged
summary:
  net_power:
    value: 450.0
    unit: MW
  heat_rate:
    value: 6200
    unit: kJ/kWh
variables:
  - id: st_hp.inlet.p
    value: 12000000
    unit: Pa
    display_value: 120
    display_unit: bar
    source: solved
residuals:
  - id: st_hp.energy_balance
    scaled_value: 2.1e-10
diagnostics:
  iterations: 12
  final_norm: 7.4e-9
  warnings: []
```

## 10. Validation rules

Minimum validation before compilation:

1. All component IDs are unique.
2. All port references in connections exist.
3. Connected ports have compatible domains.
4. Fluid connections have compatible or explicitly convertible media.
5. Required component parameters are present and within bounds.
6. Units are recognized and convertible to canonical SI.
7. Case fixed values reference valid variables or boundary inputs.
8. Graph has no accidental disconnected subgraphs unless explicitly allowed.
9. Degree-of-freedom check is reported before solving.
10. Custom equations/maps include declared valid ranges and extrapolation policy.

## 11. Versioning policy

- `schema_version` changes for breaking interchange changes; no compatibility adapter is promised
  during early development.
- Component `kind` + `version` identifies equation semantics.
- Model revisions are immutable once used for a simulation result.
- Simulation results record schema version, component versions, property backend versions, and solver version.
