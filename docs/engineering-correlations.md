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

Project publication accepts `artifact_type=thermox.correlation` and
`artifact_schema_version=thermox.correlation/v1`. Payload validation happens before immutable
content persistence; selected revisions resolve into the calculation request with complete
identity and checksum provenance.
