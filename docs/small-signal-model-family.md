# Small-signal model families

`thermox.index1-model-family/v3` constructs an ordered family of local A/B/C/D models from two or
more independently initialized cases in one declarative model document. Every point declares an
SI scheduling coordinate and a regime label. The workflow belongs to the service layer; the CLI is
a thin caller through `linearize-family`.

Each case is compiled, consistently initialized, linearized, and optionally subjected to the same
Jacobian, nonlinear-response, and nonlinear-trajectory gates as an ordinary
`thermox.index1-small-signal/v3` request. The family response embeds each complete point result and
also declares the common ordered state, input, and output identities. A family is rejected when
any point fails or when those identities differ, because matrices with different coordinates
cannot safely be interpolated or compared as one family.

The optional evaluator supports exact selection and one-dimensional piecewise-linear interpolation
of A, B, C, and D. Interpolation is permitted only when both bracketing points have the same regime
label and their coordinate separation does not exceed the request's positive
`maximum_interpolation_gap_si`. Extrapolation is always forbidden. A zero maximum gap therefore
allows construction and exact selection but grants no interpolation authority. These policies make
the validity boundary part of the engineering request rather than an implicit numerical choice.
They are necessary safety gates, not by themselves proof that interpolation is predictive.

The v3 held-out gate makes that distinction executable. Validation cases are declared separately
from the operating points used to form the family. Thermox independently initializes and
linearizes each validation case, predicts its A/B/C/D matrices using only its two neighboring
training points, and compares every entry with an absolute-plus-relative tolerance. The response
retains the complete reference linearization, interpolated matrices, bracketing case identities,
maximum absolute errors, maximum tolerance ratios, and per-matrix acceptance. Extrapolation,
cross-regime prediction, excessive gaps, coincident training/validation coordinates, and failed
matrix acceptance are structured result failures rather than warnings.

This is cross-validation against an independently solved Thermox operating point. It can qualify
a chosen scheduling coordinate and interpolation interval, but it remains internal physics/model
evidence unless the held-out case itself is anchored to an independent measured or authoritative
reference.

This contract is distinct from the perturbation ladder:

- a perturbation ladder measures the local-linearity range around one initialized point;
- a model family measures how the local model changes between independently defined physical
  operating points.

The first multi-regime family gate uses the same dynamic rigid-water-volume topology at four
case-owned initial states spanning liquid, two-phase, and vapor regions. All four points retain the
conserved states `vessel.mass` and `vessel.total_energy`, the heat input has the expected common
energy rate gain, and the pressure/enthalpy C matrices change with the local CoolProp-backed
thermodynamic derivatives. A second analytical gate uses different case-owned storage capacities
and verifies that each point retains its distinct B matrix. A third capacity case is withheld from
construction; the interpolated model reproduces its independently linearized A/B/C/D matrices
within the declared numerical tolerance. A deliberately mislocated validation coordinate is
rejected, proving that acceptance is based on matrix agreement rather than case identity.

The public AGTF30 transient benchmark currently contains one dynamically qualified operating
point. Four additional points exist in steady graphs, but they fix shaft speed rather than declare
the equivalent released transient inputs and initialized inertia states. They are therefore not
silently promoted into this family. The next AGTF30 family gate requires explicit, traceable
conversion of each point's fuel/extraction boundaries and rotor-energy initial conditions, followed
by independent nonlinear validation at every promoted point.
