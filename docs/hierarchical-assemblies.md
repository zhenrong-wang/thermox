# Hierarchical component assemblies

Thermox topology documents may group ordinary registered components into recursive `assemblies`.
An assembly is declaration-time hierarchy, not a new numerical or physical component type. It owns
no equations. Before steady or transient compilation, Thermox validates and deterministically
expands the hierarchy into the same flat component graph consumed by the existing compiler.

This supports systems at different modeling resolutions without introducing machine-specific
solver paths. A compressor may be represented by one map-driven component, by LP/IP/HP section
components with bleed splitters, or by seventeen stage components. The choice belongs to the model
author and available engineering data.

## Contract

An assembly declares:

- a local namespace of `components` and nested `assemblies`;
- internal `connections`;
- public `ports` that resolve to child component or nested-assembly ports;
- public `parameters` that resolve to declared child parameters.

```json
{
  "id": "compressor",
  "label": "Two-stage compressor",
  "ports": [
    {"name": "inlet", "endpoint": "stage_01.inlet"},
    {"name": "outlet", "endpoint": "stage_02.outlet"}
  ],
  "parameters": [{
    "name": "high_stage_pressure_ratio",
    "target": "stage_02.pressure_ratio"
  }],
  "components": [{
    "id": "stage_01",
    "kind": "compressor.fluid.isentropic_efficiency",
    "parameters": {"pressure_ratio": 3.0, "eta_is": 0.88},
    "media": {"inlet": "air", "outlet": "air"}
  }, {
    "id": "stage_02",
    "kind": "compressor.fluid.isentropic_efficiency",
    "parameters": {"pressure_ratio": 4.0, "eta_is": 0.86},
    "media": {"inlet": "air", "outlet": "air"}
  }],
  "connections": [{
    "id": "stage_01_to_stage_02",
    "kind": "fluid_link",
    "from": "stage_01.outlet",
    "to": "stage_02.inlet"
  }]
}
```

Top-level connections use the assembly like a meta-component:

```json
{
  "id": "ambient_to_compressor",
  "kind": "fluid_link",
  "from": "ambient.outlet",
  "to": "compressor.inlet"
}
```

Cases may constrain public assembly ports and override public assembly parameters:

```json
{
  "fixed_values": {
    "compressor.inlet.m_dot": {"value": 100, "unit": "kg/s"}
  },
  "parameter_overrides": {
    "components.compressor.parameters.high_stage_pressure_ratio": 5.0
  }
}
```

Stage-level case values and calibration targets remain available through explicit hierarchical IDs,
for example `compressor/stage_02.shaft.omega` and
`components.compressor/stage_02.parameters.eta_is`.

## Expansion invariants

- `/` is the stable hierarchy separator. `compressor/stage_01` is the executable component ID.
- Local component and assembly IDs share one namespace and cannot contain `.` or `/`.
- A public port must resolve to an existing direct child port or exported nested-assembly port.
- A public parameter must resolve to a parameter declared by a direct child or exported nested
  assembly parameter.
- Connections are rewritten to their ultimate component ports before the registered connector
  contract is validated.
- Public case variables, parameter overrides, and calibration targets are rewritten before
  compilation; collisions caused by expansion are rejected.
- Expanded components retain their original registered kinds, media/material bindings, engineering
  artifacts, parameters, derivatives, and result contracts.
- Results and execution provenance retain every expanded stage rather than replacing the internal
  machine with an opaque aggregate equation.

These invariants make hierarchy independent of compressor, turbine, heat-exchanger, pipe-network,
or nuclear-system semantics. Nested assemblies use the same rules recursively.

## Persistence and interfaces

Project model revisions preserve the hierarchy in canonical topology JSON. Execution expands an
immutable model/case composition without mutating the persisted definition. Direct declaration and
service clients can atomically upsert and remove assemblies with the
`thermox.assembly_definition/v1` fragment contract. The canvas presents a top-level assembly as a
collapsed meta-component, resolves its exported connector contracts through nested children, and
aggregates descendant definition readiness. This is a projection of the same declaration—not a
second execution model. The topology workspace can group selected top-level components into an
assembly as one immutable edit: internal connections move into the hierarchy, connected boundary
ports are exported, and external connections are rewritten. Ungrouping, expanded internal editing,
and the reverse connection rewrite are also published atomically; identifier collisions are
rejected rather than silently renamed or overwritten. Expanded in-place internal editing,
public-parameter selection, and publishing reusable assembly templates into the library remain
product-layer capabilities and do not change the compiler contract.

## Engineering limits

Hierarchy does not manufacture missing physics or data. A seventeen-stage compressor calculation
still requires an appropriate model and parameters or maps for each represented stage, plus explicit
splitters, ducts, cooling-air branches, shaft relationships, and losses. A coarse OEM whole-machine
map cannot be divided into credible stage maps without supporting engineering information.
