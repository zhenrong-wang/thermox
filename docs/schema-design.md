# Thermox Schema Design Draft

_Date: 2026-07-27_

This document defines the current interchange concepts for the thermal balance calculation
platform. The project is in early development: the schema is intentionally allowed to change
without backward-compatibility adapters while the architecture is established.

Implementation status: the first-class `thermox_platform` C++ module parses and validates the
`thermox.model/v2` boundary for media, registry-derived component ports, connections, cases, and
scalar unit normalization. Its metadata-backed component registry and graph compiler validate port contracts
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
schema_version: thermox.model/v2
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
package_version: 8.0.0
options:
  reference_state: IAPWS_IF97
  valid_region_policy: error
```

Recommended backend IDs:

- `coolprop_if97`: water/steam IF97 style calculations.
- `coolprop_heos`: Helmholtz-energy water/steam, including smooth phase-boundary traversal.
- `ideal_gas_mixture`: lightweight gas model for early Brayton/HRSG work.
- `cantera_solution`: combustion/reacting gas mixtures.
- `refprop`: optional high-accuracy licensed backend adapter.

## 4. Component instance

```yaml
id: st_hp
label: HP Steam Turbine
kind: turbine.steam.isentropic_efficiency
version: 1.0.0
media:
  inlet: water_steam
  outlet: water_steam
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

Port names, domains, directions, and maximum connection counts come exclusively from the resolved
component descriptor. Component instances bind a declared medium only to their fluid ports and a
declared material only to their composition-aware material ports. Heat, shaft, electrical, signal,
and control ports require no instance declaration.

```yaml
kind: turbine.fluid.isentropic_efficiency
media:
  inlet: water_steam
  outlet: water_steam
```

Canonical solver variables:

- `m_dot` — kg/s; positive follows the declared port direction and negative represents reversal.
- `p` — Pa.
- `h` — J/kg.

`T`, entropy, density, phase, and vapor quality are property-derived results, not redundant
connector unknowns. A normal directed `fluid_link` equates `m_dot`, `p`, and `h` at its two ends.
Pressure-loss, heat-transfer, mixing, and junction behavior belongs to registered components rather
than hidden connection semantics.

A steady case may nevertheless specify `component.port.T` in `fixed_values`. The compiler lowers
that engineering boundary condition into a property-backed PH equation; it does not create a
temperature unknown. This allows natural `(p,T)` boundary input while preserving a non-redundant
connector formulation.

### 5.2 Material domain

Reacting and composition-aware streams use a model-level material definition:

```yaml
materials:
  - id: wet_air
    backend: cantera
    mechanism: gri30.yaml
    phase: gri30
    package_version: 3.2.0
    species: [N2, O2, H2O]
```

Component instances bind material ports through `materials`, separately from fixed-composition
fluid `media`. The canonical variables are `p`, `h`, and one `m_dot[species]` in kg/s for every
species in the declared basis. Species mass flow is transported directly so connections and
components can conserve each species without redundant fraction-normalization equations. A
`material_link` equates that complete, material-specific variable set.

`source.material.fixed_composition` supplies a material boundary whose total mass flow remains an
unknown available to the connected graph. Its species-keyed parameters use the catalog template
`mass_fraction[{species}]`; an instance supplies only the nonzero bounded values in its bound
material. Omitted mechanism species have a mass fraction of zero:

```yaml
kind: source.material.fixed_composition
parameters:
  mass_fraction[N2]: 0.7552
  mass_fraction[O2]: 0.2314
  mass_fraction[H2O]: 0.0134
```

Supplied fractions must sum to one and cannot reference species outside the material basis. The
component adds `N-1` independent, sparse linear ratio equations using the largest fraction as the
numerical reference. Pressure and enthalpy remain ordinary boundary specifications, while one
downstream physical closure—such as a sloped performance map against a fixed discharge
pressure—can determine total flow. This avoids fixing every species flow merely to state inlet
composition.

Cases may fix `T` on either fluid or material ports. A material temperature specification uses
the port pressure, enthalpy, normalized species flows, and the bound thermochemistry package's PH
capability; temperature remains a derived state rather than an additional connector variable.

### 5.3 Heat domain

Canonical variables:

- `Q_dot` — W.
- `T` — K.

### 5.4 Shaft domain

Canonical variables:

- `W_dot` — W.
- `omega` — rad/s where maps require speed.
- optional torque.

### 5.5 Electrical domain

Canonical variables:

- `P` — active electrical power, W.
- `frequency` — electrical frequency, Hz.

The `thermox.connector.electrical/v1` contract is distinct from the shaft contract. A generator
converts shaft power and angular speed into electrical power and frequency; a shaft train balances
driver power against mechanical loads and explicit losses.

### 5.6 Signal/control domain

Signal ports do not create conservation equations automatically. They supply scalar/vector values to component equations.
The built-in contracts currently carry normalized dimensionless values. Registered proportional
and first-order-lag components provide algebraic and dynamic control paths without embedding
equipment-specific actuator semantics.

## 6. Connection

```yaml
id: c_main_steam
from: hrsg_hp_superheater.outlet
to: st_hp.inlet
kind: fluid_link
contract_version: thermox.connector.fluid/v1
parameters:
  pressure_loss:
    model: fixed_fraction
    value: 0.02
```

Connection kinds are required and must match the registry-owned endpoint domain. A model may pin
the exact domain contract with `contract_version`; compilation rejects a mismatch.

- `fluid_link`: normal directed fluid connection.
- `material_link`: composition-aware stream connection with a declared species basis.
- `heat_link`: heat transfer connection.
- `shaft_link`: mechanical power/speed connection.
- `electrical_link`: electrical power/frequency connection.
- `signal_link`: control/equation signal.

Every built-in port currently has maximum connection count `1`. A direct fan-out is invalid because
equating one stream to several streams would duplicate flow rather than conserve it. Branching,
joining, or distribution uses explicit registered mixer, splitter, or junction components. Link
contracts only equate the canonical variables of their domain; pressure loss, heat transfer,
mixing, and other physical behavior belongs to components.

## 7. Case document

```yaml
id: design_100pct
label: 100% design load
mode: steady_state_design
parameter_overrides:
  components.cooling_split.parameters.outlet_a_fraction:
    value: 8
    unit: "%"
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
  residual_tolerance: 1.0e-8
  max_iterations: 80
```

`parameter_overrides` changes registered component parameters for one operating case without
duplicating topology. Targets use the explicit
`components.<component-id>.parameters.<parameter-name>` path. Compilation validates the component,
parameter, dimension, and registered bounds before applying the override. These are declared
operating inputs, not hidden calibration values; the canonical model retains them per case.

`solver_options` in a case are descriptive model metadata. The service command owns executable
solver settings; Thermox never silently merges case metadata with command defaults. Every
effective command setting is recorded in `thermox.result/v3`.

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
names, domains, directions, maximum connection counts, fluid-property capabilities, supported
simulation modes, and formal parameter descriptors. Each parameter descriptor records its SI
dimension, requiredness, optional
default, lower/upper bounds, and whether each bound is inclusive. The compiler rejects missing,
unknown, dimensionally incompatible, non-finite, and out-of-range values before equation assembly.
Plain numeric values remain implicit SI; unit-bearing values must match the declared dimension.
Descriptors may expose one keyed name template such as `mass_fraction[{species}]`; concrete
instance values and case overrides retain the same dimension and bound validation, while the
component validates keys against its bound material context.

The compiler resolves model media
through the property registry and injects ideal-gas, CO2, or IF97 packages into property-aware
compressor, turbine, and pump equations. The default registry also contains enthalpy-flow mixer
and splitter models, an isenthalpic pressure-ratio valve, a two-stream fixed-duty heat exchanger,
a property-backed counterflow-UA heat exchanger, quality-target evaporator and condenser models,
and a rigid adiabatic fluid volume. Heat-exchanger sides may select different media, but each side
must be internally consistent. Phase-change components expose typed heat ports and accept inclusive
outlet quality from exact saturated liquid (`0`) through exact saturated vapor (`1`). Broader
parameter schemas and frontend display metadata remain future extensions.

`ComponentRegistry::descriptors()` returns a stable, kind-ordered snapshot. `thermox_service`
publishes that snapshot together with property backend IDs and connector-domain contracts as
`thermox.catalog/v5`, including physical-template identity, calculation-model labels,
internal-state names, dimensions, kinds, connector link contracts,
numerical connector metadata, and a deterministic
runtime fingerprint. Optional model behavior reads
defaults from this same descriptor rather than duplicating them inside the equation implementation.
Native hosts can assemble and inject an immutable runtime with additional registered C++ models;
transport adapters remain independent of native registry types.

Default catalog composition uses independent boundary, storage, turbomachinery, and fluid-transport
registrars plus dedicated heat-transfer/phase-change and fluid-inventory registrars. These modules
share a small internal support layer for compiled-variable lookup, medium-package lookup, required
parameter access, and property-failure translation. The graph compiler depends only on the public
`ComponentModel` contract and is unaffected by which registrar owns an implementation.

`heat_exchanger.fluid.counterflow_ua` uses counterflow terminal temperature differences obtained
from PH flashes and enforces `Q_dot = UA * LMTD`. `UA` accepts `W/K`, `kW/K`, or `MW/K`. A
temperature crossover is a recoverable model-evaluation failure during Newton line search.

`evaporator.fluid.fixed_outlet_quality` and `condenser.fluid.fixed_outlet_quality` conserve mass,
apply an optional fractional pressure loss, target outlet enthalpy directly from the registered
fluid's saturation pair, and expose positive
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

`volume.fluid.rigid_adiabatic` and `volume.fluid.rigid_heat_transfer` are transient-only. Both
store `mass` and `total_energy` as differential states and use algebraic `pressure` and `enthalpy`
with PH density/internal-energy closures. The heat-transfer variant adds an incoming heat port and
includes `Q_dot` in the energy accumulation equation. Their `volume` parameter accepts `m3`,
`m^3`, or `L`; initial mass and total energy accept `kg` and `J`/`kJ`/`MJ`. See
[Regime-spanning rigid fluid volume](regime-spanning-rigid-fluid-volume.md) for the verified phase
boundary and current applicability limits.

`volume.fluid.equilibrium_two_phase_correlated_outlet` is the correlation-aware specialization
for separated phase transport. It retains differential `mass` and `total_energy`; pressure and
holdup quality follow from rigid-volume saturation closures. A bound `void_fraction_correlation`
then maps live saturation properties, outlet flow, geometry, and transported quality to the
inventory void fraction, closing a distinct outlet quality and enthalpy. Keeping inventory and
transport qualities separate preserves the conservative finite-volume contract under phase slip.
See [Engineering correlations](engineering-correlations.md#two-phase-void-fraction).

Correlation artifact inputs may carry qualified SI operating ranges. These constraints travel with
the immutable artifact through project publication, execution snapshots, and durable job payloads;
the evaluator rejects an out-of-envelope operating point before component residual evaluation.
They qualify engineering data without placing OEM- or correlation-specific ranges in registered
component code.

`thermox.correlation/v1` packages one or more named, regime-labelled candidates behind that same
typed artifact role. Applicability determines eligibility, integer priority resolves intentional
overlap, and equal-priority ambiguity or zero-candidate coverage gaps fail explicitly. Components
continue to bind one immutable artifact ID and therefore require no regime-specific schema.

Before producing a solver problem, each compiler compares the number of unknowns and residual
equations. Under- and over-specified graphs are rejected with model-level counts plus unmatched
variable or equation candidates from bipartite incidence matching. Equations without declared
sparsity are treated conservatively as potentially depending on every variable, and candidates
are therefore guidance rather than a claim that one unique specification is wrong. Equal-count
graphs are also rejected before solver construction when matching leaves both equation and
variable candidates unmatched. The same shared incidence analysis localizes connected
underdetermined and overdetermined equation/variable regions, so diagnostics identify the affected
subgraph rather than only one matching-dependent candidate. Fixed sparse patterns additionally
enable strict structural matching in the numerical core.

For steady closed loops, the compiler classifies linear equations incrementally. A generated
connection equation is omitted only when its coefficients and right-hand side are a consistent
linear combination of already retained equations. Component equations and case specifications are
never automatically removed, so user over-specification remains an error. The compiled graph
records every omitted residual name in `reduced_connection_equations`; independent stream
properties, such as Rankine-loop enthalpy closure, remain enforced.

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
schema_version: thermox.result/v3
graph:
  components:
    - component_id: st_hp
      kind: turbine.fluid.isentropic_efficiency
      ports:
        - port_name: inlet
          domain: fluid
          medium_id: water_steam
          phase: vapor
          primary_values:
            - {name: p, dimension: pressure, value_si: 12000000}
            - {name: h, dimension: specific_enthalpy, value_si: 3400000}
          derived_values:
            - {name: T, dimension: temperature, value_si: 813.2}
      internal_values: []
      metrics: []
  system_balances:
    - {name: net_boundary_mass_flow, dimension: mass_flow, value_si: 0}
    - {name: net_boundary_energy_flow, dimension: power, value_si: 0}
  kpis: []
```

Steady responses contain one `graph`. Each transient trajectory and event sample contains the same
graph structure; transient primary and internal values also carry `derivative_si_s`. Fluid
temperature, density, entropy, phase, quality, heat capacities, speed of sound, viscosity, and
thermal conductivity are derived through the selected property package. Material ports expose the
same thermodynamic fields plus mean molecular weight through the selected thermochemistry package.
Other domains expose their canonical primary values without thermodynamic fields.

Steady and transient results audit system boundaries directly from registered component/port
semantics and graph connectivity. Explicit source/sink components declare their boundary role;
every unconnected directional ordinary-equipment port is also an external boundary. Positive
values enter the modeled system.
Fluid and material streams contribute mass and enthalpy flow, while heat, shaft, and electrical
ports contribute energy flow. A nonzero net energy flow is not silently labeled a solver error: it
may identify modeled conversion losses or an omitted energy carrier that must be reported
separately. For transient samples, the net boundary energy flow is expected to equal the summed
instantaneous storage rate rather than zero.

Every steady component with at least two directional energy or material ports also reports
`net_mass_flow` and/or `net_energy_flow` metrics. The same positive-inward sign convention applies.
Lossless equipment should approach zero; positive energy flow on a shaft train or generator
quantifies modeled conversion loss. Summing those losses explains the corresponding nonzero system
boundary energy flow without inventing a plant-wide correction parameter.

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
- Simulation results use `thermox.result/v3` and record the platform build, model and command
  schemas, catalog fingerprint, requested and resolved component/property versions, connector
  contracts, solver contract, and every effective solver setting.
