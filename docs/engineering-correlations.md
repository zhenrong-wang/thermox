# Engineering correlation artifacts

`thermox.correlation/v1` stores a bounded algebraic engineering correlation independently from a
component instance. Its payload declares typed inputs, one typed output, immutable coefficients,
and a safe expression. Expressions use the same bounded grammar and analytic differentiation
engine as declarative expression components; they cannot perform I/O, assignment, loops, or call
arbitrary code.

```json
{
  "inputs": [
    {"name": "mass_flow", "dimension": "mass_flow"},
    {"name": "density", "dimension": "density"},
    {"name": "area", "dimension": "area"}
  ],
  "output": {"name": "pressure_loss", "dimension": "pressure"},
  "coefficients": {"loss_coefficient": 1.5},
  "expression": "loss_coefficient * mass_flow * abs(mass_flow) / (2 * density * area * area)"
}
```

The initial consumer is the `fitting.fluid.return_bend.correlation` calculation model under the
existing `fitting.fluid.return_bend` physical template. A component instance supplies its diameter
and binds the artifact role `pressure_loss_correlation`. The model supplies live mass flow, fluid
density, flow area, and diameter to the correlation and chains its analytic derivatives into the
system Jacobian.

The artifact registry and evaluator are component-neutral. Other component models can declare a
`thermox.correlation` role and define which physical quantities they supply. This keeps the
correlation dataset separate from topology, component parameters, and fluid-property providers.

## Two-phase void fraction

`pipe.fluid.void_fraction_correlation_local_loss` binds the artifact role
`void_fraction_correlation`. The output must be dimensionless and the evaluated value must satisfy
`0 < alpha < 1`. A correlation may declare any subset of these component-supplied inputs:

| Input | Dimension | Meaning |
| --- | --- | --- |
| `vapor_quality` | `dimensionless` | Vapor mass quality at mean endpoint pressure and transported enthalpy |
| `liquid_density` | `density` | Saturated-liquid density at mean pressure |
| `vapor_density` | `density` | Saturated-vapor density at mean pressure |
| `mass_flow` | `mass_flow` | Signed component mass flow |
| `area` | `area` | Pipe flow area |
| `diameter` | `length` | Pipe flow diameter |
| `pressure` | `pressure` | Mean endpoint pressure |

For example, the constant-slip closure can be supplied as engineering data rather than compiled
component logic:

```json
{
  "inputs": [
    {"name": "vapor_quality", "dimension": "dimensionless"},
    {"name": "liquid_density", "dimension": "density"},
    {"name": "vapor_density", "dimension": "density"}
  ],
  "output": {"name": "void_fraction", "dimension": "dimensionless"},
  "coefficients": {"slip_ratio": 2.0},
  "expression": "1 / (1 + ((1 - vapor_quality) / vapor_quality) * (vapor_density / liquid_density) * slip_ratio)"
}
```

The component uses the resulting void fraction to calculate volume-weighted mixture density and
then applies its declared local-loss and elevation-head equation. It requires registered
`state_ph` and `saturation_p` capabilities and a strictly two-phase mean state. Missing artifacts,
unsupported input names, wrong dimensions, unsafe expressions, evaluation failures, and
nonphysical outputs are rejected explicitly. The same contract compiles in steady and transient
graphs.

Project publication accepts `artifact_type=thermox.correlation` and
`artifact_schema_version=thermox.correlation/v1`. Payload validation happens before immutable
content persistence; selected revisions resolve into the calculation request with complete
identity and checksum provenance.

The Definition workspace exposes the same contract through a dedicated Engineering Data Registry
form. It uses catalog unit dimensions for every input and output, supports immutable coefficient
sets, and separates creation from revision. Revising retrieves the exact immutable parent payload
through the tenant-scoped service API, verifies its stored size and checksum, and preloads the
authoring form before publishing a child revision. Component forms do not treat correlations as
equipment: they offer a correlation only when the selected calculation model declares a compatible
`thermox.correlation` artifact role.
