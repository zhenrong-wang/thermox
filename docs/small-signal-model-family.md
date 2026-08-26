# Small-signal model families

`thermox.index1-model-family/v1` constructs an ordered family of local A/B/C/D models from two or
more independently initialized cases in one declarative model document. The workflow belongs to
the service layer; the CLI is a thin caller through `linearize-family`.

Each case is compiled, consistently initialized, linearized, and optionally subjected to the same
Jacobian, nonlinear-response, and nonlinear-trajectory gates as an ordinary
`thermox.index1-small-signal/v3` request. The family response embeds each complete point result and
also declares the common ordered state, input, and output identities. A family is rejected when
any point fails or when those identities differ, because matrices with different coordinates
cannot safely be interpolated or compared as one family.

This contract is distinct from the perturbation ladder:

- a perturbation ladder measures the local-linearity range around one initialized point;
- a model family measures how the local model changes between independently defined physical
  operating points.

The first multi-regime gate uses the same dynamic rigid-water-volume topology at four case-owned
initial states spanning liquid, two-phase, and vapor regions. All four points retain the conserved
states `vessel.mass` and `vessel.total_energy`, the heat input has the expected common energy rate
gain, and the pressure/enthalpy C matrices change with the local CoolProp-backed thermodynamic
derivatives. A second analytical gate uses different case-owned storage capacities and verifies
that each point retains its distinct B matrix.

The public AGTF30 transient benchmark currently contains one dynamically qualified operating
point. Four additional points exist in steady graphs, but they fix shaft speed rather than declare
the equivalent released transient inputs and initialized inertia states. They are therefore not
silently promoted into this family. The next AGTF30 family gate requires explicit, traceable
conversion of each point's fuel/extraction boundaries and rotor-energy initial conditions, followed
by independent nonlinear validation at every promoted point.
