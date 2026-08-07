# Regime-map architecture

Many thermofluid closures are valid only inside a physical regime. Examples include two-phase
flow-pattern-dependent pressure drop, boiling heat transfer, dryout, choking, and turbomachinery
operating regions. A converged algebraic solve does not prove that the selected closure is
physically applicable.

Thermox therefore represents classification separately from a numeric correlation. The platform
contract is `thermox.regime_map/v1`, implemented by `RegimeMapArtifact`. It is an immutable
engineering artifact registered through the same provider-neutral `EngineeringArtifactRegistry`
used by performance maps and correlations.

## Contract

A regime map declares:

- typed named inputs;
- stable region IDs and engineering regime labels;
- an integer priority for intentional nested regions; and
- one or more bounded scalar criteria per region.

Each criterion uses the bounded `SafeExpression` grammar and an SI interval with explicit inclusive
or exclusive endpoints. All criteria in a region must pass. If several regions pass, only the
unique highest-priority region is selected. Equal-priority overlap is an error, as is a gap where
no region applies. This makes map boundaries auditable and prevents vector order from silently
choosing physics.

The expression grammar contains no assignments, loops, callbacks, file access, or arbitrary user
code. Artifact limits bound the number of inputs, regions, criteria, and expression nodes.

## Separation from closure correlations

A regime map answers “which declared physical region contains this state?” A correlation answers
“what continuous value does this closure produce?” Keeping these contracts distinct allows the
same classified regime to select pressure-drop, void-fraction, and heat-transfer closures without
using magic numeric regime codes.

The first milestone provides the platform artifact and deterministic classifier. The next
integration step is to add request/persistence payloads and optional component artifact roles, then
pass the selected regime to compatible correlation families. That integration must preserve an
explicit error when the map selects a regime for which no closure candidate exists.

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

The synthetic regions in unit tests verify selection semantics only and make no physical claim.
