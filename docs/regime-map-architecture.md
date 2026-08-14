# Regime-map architecture

Many thermofluid closures are valid only inside a physical regime. Examples include two-phase
flow-pattern-dependent pressure drop, boiling heat transfer, dryout, choking, and turbomachinery
operating regions. A converged algebraic solve does not prove that the selected closure is
physically applicable.

Thermox therefore represents classification separately from a numeric correlation. The platform
contract is `thermox.regime_map/v2`, implemented by `RegimeMapArtifact`. It is an immutable
engineering artifact registered through the same provider-neutral `EngineeringArtifactRegistry`
used by performance maps and correlations.

## Contract

A regime map declares:

- typed named inputs;
- stable region IDs and engineering regime labels;
- an integer priority for intentional nested regions;
- one or more named alternative branches per region, each with explicit priority; and
- one or more bounded scalar criteria per branch.

Each criterion uses the bounded `SafeExpression` grammar and an SI interval with explicit inclusive
or exclusive endpoints. All criteria in one branch must pass, while any branch may establish its
region: this is an explicit disjunction of named physical mechanisms. The unique highest-priority
matching branch is reported in `RegimeMapEvaluation`. If several regions pass, only the unique
highest-priority region is selected. Equal-priority overlap is an error at either level, as is a
gap where no region applies. This makes alternative mechanisms and precedence auditable and
prevents vector order from silently choosing physics.

The expression grammar contains no assignments, loops, callbacks, file access, or arbitrary user
code. Artifact limits bound the number of inputs, regions, branches, criteria, and expression
nodes. The clean v2 contract replaces the earlier flat-region draft; Thermox is unreleased and
does not retain that draft as a compatibility format.

## Separation from closure correlations

A regime map answers “which declared physical region contains this state?” A correlation answers
“what continuous value does this closure produce?” Keeping these contracts distinct allows the
same classified regime to select pressure-drop, void-fraction, and heat-transfer closures without
using magic numeric regime codes.

The service contract carries regime maps in `SimulationArtifactBundle`. Team/project artifact
publication accepts `artifact_type=thermox.regime_map` and
`artifact_schema_version=thermox.regime_map/v2`, canonicalizes and validates the payload before
content-addressed persistence, and resolves an exact revision into job snapshots. Regions,
branches, priorities, and criteria participate in job idempotency fingerprints and PostgreSQL job
encoding. The containing job contract is `thermox.job/v16`.

An immutable Study may apply a dimensioned operating envelope to any declared regime-map input.
The policy is distinct from the source map's classification criteria: it restricts where that exact
artifact revision is authorized to execute. Classification fails before region selection outside
the policy, using the same `artifact_operating_envelope_violation` service contract as performance
maps and correlations.

The generic `pipe.fluid.correlated_two_phase_pressure_drop` model exposes the optional
`friction_regime_map` artifact role. When bound, compilation verifies the property backend's
surface-tension capability, validates every requested live input, and requires every map regime to
have an exact candidate `flow_regimes` route or an explicitly declared general fallback. Candidate
`regime` labels describe the correlation's own applicability taxonomy and are not overloaded as
flow-pattern names. Runtime derives the groups from the current two-phase state, classifies the
physical regime, prefers exact routes, and uses fallback candidates only where no exact route was
declared. The selected fallback is reported in the evaluation provenance. A map gap, ambiguous
region, missing route, or invalid derived quantity is an explicit error. Without this optional
binding, correlation-family selection remains independent.

## Physical data requirements

Thermox does not ship a guessed universal flow-pattern map. A packaged map must identify its
source, geometry, orientation, working-fluid basis, dimensionless groups, and validity envelope.
Many established gas–liquid maps require superficial velocities, density and viscosity ratios,
Froude/Weber-type groups, and surface tension. The property contract now exposes saturation-pair
surface tension as a distinct capability with no fallback constant. The reusable
`calculate_two_phase_flow_groups` function derives phase mass fluxes, superficial velocities,
phase Reynolds/Froude/Weber numbers, Bond number, and phase-property ratios from one validated SI
input contract. Published maps remain responsible for declaring any map-specific transformed
coordinates and validity ranges.

The default `RegimeMapTemplateRegistry` contains three deliberately separate Mishima–Ishii
vertical-upflow transition templates: bubbly-to-slug at void fraction 0.30, the mechanistic
slug-to-churn boundary for a round tube, and the high-velocity entrainment transition to annular
flow. They remain separate because the source mechanisms have different inputs and evidence.
The slug-to-churn template exposes the independent assessment's warning that churn data were
insufficient to validate that boundary and slug-flow characterization was poor. The annular
template exposes its liquid-viscosity-number and liquid-Reynolds limits.

A fourth composite template uses the v2 branch contract to combine those boundaries with the
Mishima–Ishii liquid-film-reversal annular mechanism. It returns bubbly, slug, churn, or annular;
the annular region records whether film reversal or wave entrainment selected it. Annular has
higher region priority, allowing the cited entrainment mechanism to bypass an intermediate regime.
Because one component boundary has the narrower `N_mu < 1/15` and liquid `Re > 1635` envelope,
the complete composite conservatively applies those gates to every branch. Its void fraction comes
from the independently bound closure, so changing that closure changes classification and remains
part of the model provenance. This is a cited, limited engineering map—not a universal truth—and
the catalog repeats the weak slug/churn evidence warning.

Outside a declared domain, classification returns no applicable region rather than extrapolating.
Every template exposes its citation and scope through `thermox.catalog/v10`.
Direct callers use `instantiate_regime_map_template`; service and remote callers use
`SimulationService::instantiate_regime_map` or
`POST /api/v1/regime-map-artifacts/instantiate`. The result is an ordinary canonical
`thermox.regime_map/v2` payload, a deterministic content SHA-256, and the runtime-catalog
fingerprint. Publishing it as an immutable Team/project artifact remains a separate operation.
Native extension packages can register additional templates without changing the solver or
service contract.

Synthetic regions in unit and connected-pipe tests verify selection semantics only and make no
physical claim. The packaged Mishima–Ishii transitions and composite map have separate equation,
regime-side, alternative-mechanism, metadata-limitation, and out-of-domain refusal tests.
The composite map is additionally exercised in a connected IF97 two-phase pipe with the packaged
Chisholm family declared as a general fallback. This validates the artifact-routing path; it does
not convert Chisholm into a regime-specific correlation or expand either source's validity.
