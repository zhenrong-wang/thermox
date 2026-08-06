# Engineering correlation artifacts

`thermox.correlation/v1` stores a bounded algebraic engineering correlation family independently
from a component instance. Its payload declares shared typed inputs, one typed output, and one or
more candidate laws. Each candidate owns its regime, priority, immutable coefficients, safe
expression, and optional qualified operating ranges. Expressions use the same bounded
grammar and analytic differentiation engine as declarative expression components; they cannot
perform I/O, assignment, loops, or call arbitrary code.

```json
{
  "inputs": [
    {"name": "mass_flow", "dimension": "mass_flow"},
    {"name": "density", "dimension": "density"},
    {"name": "area", "dimension": "area"}
  ],
  "output": {"name": "pressure_loss", "dimension": "pressure"},
  "candidates": [{
    "id": "default",
    "regime": "general",
    "priority": 0,
    "coefficients": {"loss_coefficient": 1.5},
    "expression": "loss_coefficient * mass_flow * abs(mass_flow) / (2 * density * area * area)",
    "applicability": [{
      "input": "mass_flow",
      "minimum": 0.0,
      "maximum": 25.0,
      "minimum_inclusive": true,
      "maximum_inclusive": false
    }]
  }]
}
```

Applicability limits are expressed in the declared input's SI dimension. Each range must reference
one shared input, may provide either or both finite bounds, and explicitly controls whether each
present bound is inclusive. Duplicate inputs, empty ranges, and unknown inputs are rejected when
the immutable artifact revision is published. At runtime, evaluation outside a qualified range is
rejected before the expression is evaluated; the diagnostic identifies the artifact input, live
value, and qualified interval. Omitting a candidate's `applicability` means that the artifact owner has declared
no machine-readable operating envelope—it does not imply universal physical validity.

The initial consumer is the `fitting.fluid.return_bend.correlation` calculation model under the
existing `fitting.fluid.return_bend` physical template. A component instance supplies its diameter
and binds the artifact role `pressure_loss_correlation`. The model supplies live mass flow, fluid
density, flow area, and diameter to the correlation and chains its analytic derivatives into the
system Jacobian.

The artifact registry and evaluator are component-neutral. Other component models can declare a
`thermox.correlation` role and define which physical quantities they supply. This keeps the
correlation dataset separate from topology, component parameters, and fluid-property providers.

## Two-phase pressure drop

`pipe.fluid.correlated_two_phase_pressure_drop` separates two independent engineering closures:
`void_fraction_correlation` and `friction_pressure_gradient_correlation`. The first output must be
dimensionless and satisfy `0 < alpha < 1`. The second must be named
`friction_pressure_gradient`, carry dimension `pressure_gradient` (`Pa/m`), and evaluate to a
finite nonnegative magnitude. Both roles use immutable `thermox.correlation/v1` artifacts.

A void-fraction correlation may declare any subset of these component-supplied inputs:

| Input | Dimension | Meaning |
| --- | --- | --- |
| `vapor_quality` | `dimensionless` | Vapor mass quality at mean endpoint pressure and transported enthalpy |
| `liquid_density` | `density` | Saturated-liquid density at mean pressure |
| `vapor_density` | `density` | Saturated-vapor density at mean pressure |
| `mass_flow` | `mass_flow` | Signed component mass flow |
| `mass_flux` | `mass_flux` | Nonnegative `abs(mass_flow) / area` |
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
  "candidates": [{
    "id": "constant_slip",
    "regime": "general",
    "priority": 0,
    "coefficients": {"slip_ratio": 2.0},
    "expression": "1 / (1 + ((1 - vapor_quality) / vapor_quality) * (vapor_density / liquid_density) * slip_ratio)"
  }]
}
```

The friction correlation may additionally request `void_fraction`, `mixture_density`, saturated
`liquid_viscosity` and `vapor_viscosity`, `length`, or `roughness`. It owns the selected friction
law, flow-regime dependence, and qualified operating envelope. The component multiplies its
nonnegative pressure-gradient magnitude by pipe length and applies the sign of mass flow. This
keeps reverse-flow behavior in the component balance without requiring every engineering law to
reimplement it.

The full algebraic balance combines signed distributed friction, signed local loss, and elevation
head. The pipe requires registered `state_ph` and `saturation_p` capabilities and a strictly
two-phase mean state. At exactly zero flow both friction terms are zero and the friction artifact
is not evaluated. Missing artifacts, unsupported input names, wrong dimensions, unsafe
expressions, evaluation failures, and nonphysical outputs are rejected explicitly. The same
contract compiles in steady and transient graphs. Acceleration pressure drop is not silently
folded into the friction term; it remains a distinct follow-on closure.

`volume.fluid.equilibrium_two_phase_correlated_outlet` applies the same artifact contract to a
transient rigid inventory. Conserved total mass and internal energy determine pressure and
thermodynamic holdup quality. Those states determine the observable holdup void fraction, while
the bound correlation solves the transported outlet quality that reproduces that void fraction.
This distinction lets a slip or drift-flux law affect phase transport without replacing or
overconstraining the thermodynamic inventory closure. Outlet enthalpy is the saturated-mixture
enthalpy at the correlated transported quality. The component therefore exposes separate
`holdup_quality`, `void_fraction`, and `outlet_quality` results.

For this inventory consumer, `vapor_quality` means transported outlet quality, densities and
pressure are evaluated at the live inventory pressure, `mass_flow` is outlet flow, and geometry
comes from `flow_diameter`. It is transient-only, requires `saturation_p`, and rejects
nonphysical correlation outputs explicitly. Correlation validity envelopes and flow-regime
selection remain engineering-data responsibilities. The candidate applicability contract enforces
declared scalar operating ranges and deterministic selection among the laws in the bound artifact.

## Correlation families and regime selection

The same `thermox.correlation/v1` contract represents both a one-law correlation and a regime
family. A one-law correlation contains one candidate; a regime family contains multiple named
candidates:

```json
{
  "inputs": [
    {"name": "vapor_quality", "dimension": "dimensionless"},
    {"name": "mass_flux", "dimension": "mass_flux"},
    {"name": "liquid_density", "dimension": "density"},
    {"name": "vapor_density", "dimension": "density"}
  ],
  "output": {"name": "void_fraction", "dimension": "dimensionless"},
  "candidates": [{
    "id": "bubbly_flow",
    "regime": "bubbly",
    "priority": 10,
    "coefficients": {"slip_ratio": 1.2},
    "expression": "1 / (1 + ((1 - vapor_quality) / vapor_quality) * (vapor_density / liquid_density) * slip_ratio)",
    "applicability": [
      {"input": "vapor_quality", "minimum": 0.01, "maximum": 0.35},
      {"input": "mass_flux", "minimum": 0.0, "maximum": 500.0}
    ]
  }]
}
```

Selection is deterministic and independent of component kind. Candidates whose complete envelope
contains the live operating point are eligible; the unique candidate with the highest priority is
evaluated. No eligible candidate is a coverage-gap error. Multiple eligible candidates at the same
highest priority are an ambiguity error listing their IDs. Successful evaluation records the
selected candidate and regime in the correlation evaluation result. Candidate IDs must be unique,
and every candidate expression must exactly match the family inputs plus its own coefficients.

Two-phase pipe and inventory consumers additionally supply nonnegative `mass_flux = |m_dot| / area`
when requested, alongside quality, phase densities, signed mass flow, area, diameter, and pressure.
This lets engineering data classify common operating regions without adding a pipe-specific
selector to the platform. Regime labels remain declared engineering provenance; Thermox does not
invent a universal flow-pattern map. Candidate expressions should be continuous across intended
switch boundaries where possible because discontinuous switching creates a nonsmooth residual.

## Packaged correlation templates

The runtime catalog publishes versioned, provider-extensible correlation templates separately
from immutable engineering-artifact revisions. A template is a referenced equation shape with
typed inputs, typed coefficients, coefficient bounds, and an applicability envelope. Instantiating
one requires an artifact identity and all coefficients that do not have explicit defaults. The
result is an ordinary `thermox.correlation/v1` artifact, so topology and component consumers remain
independent of the template registry.

The first packaged template is the Zuber–Findlay kinematic drift-flux relation, based on
G. E. Zuber and J. A. Findlay, *Average Volumetric Concentration in Two-Phase Flow Systems*,
Journal of Heat Transfer 87(4), 1965, DOI
[10.1115/1.3689137](https://doi.org/10.1115/1.3689137). In superficial-velocity form,

`v_g = C0 j + V_gj` and `alpha = j_g / v_g`,

where

`j_g = x G / rho_g` and `j = G (x / rho_g + (1 - x) / rho_l)`.

The template therefore consumes vapor quality, saturated phase densities, and positive mass flux.
It requires the engineer to supply the distribution parameter `C0` and drift velocity `V_gj`;
Thermox does not guess them. Its declared envelope is `0 < x < 1` and `G > 0`, and its regime is
explicitly `upward_cocurrent_user_parameterized`. Those mathematical limits do not establish that
the supplied coefficients are valid for a particular diameter, orientation, pressure, fluid, or
flow pattern. Geometry- and regime-specific coefficient correlations and flow-pattern maps remain
separate engineering data that must carry their own provenance and applicability.

The limiting case `C0 = 1`, `V_gj = 0` exactly recovers homogeneous void fraction. Tests cover that
limit, coefficient rejection, analytic quality sensitivity, catalog serialization, and a connected
two-phase riser pressure balance using an instantiated template.

Project publication accepts `artifact_type=thermox.correlation` and
`artifact_schema_version=thermox.correlation/v1`. Payload validation
happens before immutable content persistence; selected revisions resolve into the calculation
request with complete identity and checksum provenance.

The Definition workspace exposes the same contract through a dedicated Engineering Data Registry
form. It uses catalog unit dimensions for every input and output, authors one or more candidates,
supports candidate-local immutable coefficient sets, and separates creation from revision. Revising retrieves the exact immutable parent payload
through the tenant-scoped service API, verifies its stored size and checksum, and preloads the
authoring form before publishing a child revision. Component forms do not treat correlations as
equipment: they offer a correlation only when the selected calculation model declares a compatible
`thermox.correlation` artifact role.
