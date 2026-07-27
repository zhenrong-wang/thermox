# Thermox Schema Design Draft

_Date: 2026-07-27_

This document defines the first stable interchange concepts for the thermal balance calculation platform. It is intentionally implementation-neutral so the graph UI, backend API, numeric compiler, and solver can evolve independently.

Implementation status: the current C++ MVP parses and validates the core `thermox.model/v1` boundary for media, components, typed ports, connections, cases, and scalar unit normalization. It also includes a metadata-backed component registry and graph compiler that validate registered component port contracts, create canonical port variables, and lower connections/fixed values into sparse equation assembly metadata. Design-only metadata such as annotations/options remains documented here for registry/frontend use and may be ignored by the first parser.

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

- `m_dot` — kg/s, signed by port orientation.
- `p` — Pa.
- `h` — J/kg.
- `T` — K, usually derived but may be explicit in selected formulations.
- `composition` — mass or mole fractions for mixtures.
- `quality` — optional for two-phase water/steam diagnostics.

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
  ambient.T:
    value: 288.15
    unit: K
  ambient.p:
    value: 101325
    unit: Pa
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
- `dynamic_initialization` (future)
- `dynamic_transient` (future)
- `parameter_sweep` (future)
- `optimization` (future)

## 8. Component type metadata schema
Component type definitions should be served to the frontend and compiler from the same registry. The current C++ MVP implements a metadata subset of this idea: registered component models expose kind/version plus required port names, domains, and directions. Specialized physical residuals and parameter validation have started with `compressor.gas.isentropic_efficiency` and `turbine.gas.isentropic_efficiency`. A production property-package interface now supports ideal gas, CO2, and IF97; injecting it into the graph-compiled component models, broader parameter schemas, and frontend display metadata remain future extensions.

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

- `schema_version` changes only for breaking interchange changes.
- Component `kind` + `version` identifies equation semantics.
- Model revisions are immutable once used for a simulation result.
- Simulation results record schema version, component versions, property backend versions, and solver version.
