# Calculation Intent Architecture

## Purpose

Thermox must identify the engineering question before solving it. A graph result has different
evidentiary meaning depending on whether its measured counterpart was an input boundary, a fitted
observation, a hard reconciliation equality, or an output excluded from all fitting.

The service distinguishes three calculation intents:

| Intent | Fixed input | Solved output | Meaning of agreement |
|---|---|---|---|
| `forward_prediction` | Declared physical model and operating boundaries | Graph states, balances, performance quantities | Potentially independent only when inputs and parameters were not derived from the compared output |
| `parameter_calibration` | Model, cases, observations, uncertainties and priors | Best-fit physical parameters | Reproduction of calibration data; residuals may be a weighted compromise |
| `data_reconciliation` | Model plus hard measured equalities | Explicitly declared adjustable boundaries or parameters | Internally consistent inverse reconstruction; constrained outputs are not predictions |

Result projection remains separate. Selecting a value for display does not make it a constraint,
objective, or validation reference.

## Forward calculation

A forward calculation is the primary physical validation path. Measured ambient conditions,
flows, fuel properties, configuration and independently justified equipment characteristics are
inputs. Temperatures, powers, heat rates and balances calculated by the graph are outputs.

An output may be classified as independent evidence only if it did not directly or indirectly set
a boundary, fitted parameter, map correction, or control schedule used for that calculation.

## Parameter calibration

Calibration minimizes uncertainty-weighted observation residuals and optional parameter priors.
It may be overdetermined and may legitimately return a best feasible result after its iteration
budget without satisfying every observation exactly. Its outputs are fitted parameters and
calibration residuals. They are never relabeled as forward predictions.

## Data reconciliation

Data reconciliation answers an inverse question such as:

> With measured power and other selected states imposed as equalities, what inlet flow makes the
> declared model mutually consistent?

The service contract reuses a named model calibration declaration only as a typed list of
adjustable targets and observations. It exposes two explicit reconciliation modes.

### Hard equalities

Hard-equality reconciliation gives those entries stricter semantics:

- every observation is a hard equality;
- the number of adjustable quantities must equal the number of hard equalities;
- all adjustable quantities require finite bounds;
- priors are rejected because they would turn an equality solve into a compromise objective;
- a finite-difference sensitivity matrix drives a bounded, line-searched Newton iteration;
- a singular matrix is reported as unidentifiable rather than regularized silently;
- success requires every normalized hard residual to meet the declared tolerance.

The observation `sigma` acts only as a numerical residual scale in this intent. It does not soften
the equality.

### Weighted measurements

Weighted-measurement reconciliation is a separate overdetermined statistical mode. It requires at
least as many measurements as adjustable quantities and minimizes the sum of squared residuals
normalized by the declared measurement standard uncertainties. A bounded, line-searched
Gauss-Newton step solves the rectangular sensitivity system with column-pivoted Householder QR.
Rank and the local covariance kernel are obtained from the same rectangular factorization; the
implementation does not form the normal information matrix and therefore does not square its
condition number.

Observations retain their marginal standard uncertainties in `sigma`. When measurements share
instrumentation, data reduction, ambient correction, or another common uncertainty source, the
calibration declaration may add pairwise `measurement_correlations` by stable observation ID:

```json
"measurement_correlations": [
  {
    "first_observation": "power_meter_a",
    "second_observation": "power_meter_b",
    "correlation": 0.5
  }
]
```

Unspecified pairs are independent. Thermox assembles the complete correlation matrix in
observation order and rejects self-pairs, duplicate pairs, unknown IDs, coefficients outside
`(-1, 1)`, and matrices that are not positive definite. A dense Cholesky factor whitens both the
normalized residual vector and sensitivity rows before objective, rank, QR-step, chi-square, and
covariance calculations. Calibration uses the same correlated objective; hard-equality
reconciliation rejects correlation declarations because measurement probability does not soften
an exact equality.

The response reports:

- sensitivity rank and a local-identifiability verdict;
- measurement count, adjustable count, and redundancy degrees of freedom;
- weighted sum of squares and reduced chi-square when redundancy exists;
- local linearized parameter standard uncertainties from `(J_w^T J_w)^-1`, where `J_w` is the
  covariance-whitened sensitivity;
- pairwise parameter correlations;
- the active-bound count and the number of locally free parameters included in uncertainty;
- an explicit uncertainty interpretation for every inferred parameter;
- whether an unsatisfied hard-equality solve is locally limited by declared physical
  bounds, including each active quantity's stable ID, bound side, fitted and bound values, and
  whether that bound blocks the local Newton step;
- the rank-revealing factorization's accepted-diagonal ratio and method, explicitly not mislabeled
  as a matrix condition number.

Per-observation `normalized_residual` remains the marginal value `(calculated - measured)/sigma`
so it is individually interpretable. `weighted_sum_squares` and reduced chi-square are the joint
Mahalanobis quantities after correlation whitening. Diagnostics explicitly disclose whether
measurement covariance was applied and how many off-diagonal pairs were declared.

Weighted reconciliation uses a local active set at finite parameter bounds. A parameter at its
lower bound whose objective gradient points lower, or at its upper bound whose gradient points
higher, is held fixed while the Gauss-Newton step is recomputed over the remaining free columns.
This lets a valid constrained optimum converge instead of repeatedly proposing a clipped zero
step. Rank and identifiability of the declared full sensitivity remain visible separately.

After convergence, covariance is factorized only over locally free sensitivity columns.
Bound-active parameters receive `standard_uncertainty_si: null`, `bound_active: true`, and the
interpretation `bound_active_one_sided_not_estimated`; Thermox does not report a fabricated
two-sided Gaussian uncertainty at a truncated optimum. Free parameters retain
`local_two_sided_linearized` uncertainties, and parameter correlations are emitted only between
free parameters. A profile-likelihood or sampling workflow is still required to quantify a
one-sided confidence limit for an active parameter.

### Profile-likelihood intervals

Profile likelihood is an explicit, optional post-reconciliation phase because each interval can
require many additional physical-model solves. For each selected parameter and direction,
Thermox fixes that parameter at trial coordinates, re-optimizes all nuisance parameters with the
same covariance-whitened active-set Gauss-Newton machinery, brackets the requested objective
increase, and bisects the bracket. It never substitutes the local covariance ellipse for these
nonlinear evaluations.

The request declares the objective increase, bracketing/bisection/nuisance-solve budgets, and an
optional list of parameter IDs. The default objective increase `3.841458820694124` is the usual
asymptotic 95% likelihood-ratio threshold for one profiled parameter; the numerical value and its
statistical interpretation remain visible rather than being called “95%” unconditionally.

Each endpoint reports its coordinate, achieved objective increase, whether the threshold was
reached, and whether a finite physical bound truncated that side. A parameter already at its
upper bound therefore has an upper endpoint equal to the estimate with `bound_truncated=true` and
`threshold_reached=false`; its opposite side can still yield a one-sided likelihood limit.
Failures and model-evaluation counts are local to each interval and do not relabel a successful
base reconciliation as failed. Profiling is disabled by default and its settings are preserved in
solver provenance.

A threshold crossing is a numerical profile result, not automatically a credible confidence
claim. Its usual likelihood-ratio coverage additionally assumes a defensible measurement model,
adequate physical model, identifiable nuisance fit, and suitable asymptotic regime. A very large
reduced chi-square or known missing physics remains visible and can invalidate that interpretation
even when the profile calculation itself succeeds; Thermox does not rescale the likelihood or
rename the interval to hide poor agreement.

The thin CLI exposes the same service contract:

```sh
thermox_cli reconcile --model model.json \
  --reconciliation weighted_fit --mode weighted-measurements \
  --profile-likelihood --profile-parameter airflow \
  --profile-objective-increase 3.841458820694124 --format json
```

Known measurement uncertainties are treated as input standard uncertainties, so covariance is not
automatically rescaled to force reduced chi-square to one. A large reduced chi-square remains
visible as evidence of inconsistent measurements, underestimated uncertainty, or missing physics.
The free-parameter covariance is a local first-order approximation. Profile likelihood provides
bounded one-dimensional nonlinear intervals, but simultaneous multidimensional confidence
regions and Monte Carlo propagation remain future work. The reference
implementation uses column-pivoted QR; an optimized sparse QR or SVD backend can replace it for
very large or extremely ill-conditioned estimation problems without changing this service
contract.

Case-owned targets make boundary reconciliation explicit:

```text
cases.<case-id>.fixed_values.<graph-variable>
cases.<case-id>.parameter_overrides.<component-parameter-path>
```

The response keeps three result classes separate:

1. `inferred_parameters`: quantities moved by the reconciliation solve;
2. `hard_constraints`: measured values that the solution was required to satisfy;
3. `held_out_results`: comparisons evaluated only after reconciliation and excluded from the
   sensitivity system.

A held-out target cannot also be a hard constraint for the same case. Held-out agreement after
reconciliation is normally `boundary_constrained`, not automatically `independent_reference`.

Hard reconciliation keeps three failure classes distinct. A rank-deficient sensitivity is
`reconciliation_unidentifiable`; failure to find a residual-reducing step with no active bound is
`reconciliation_line_search_failed`; and the same condition where the local Newton step points
outward through an active adjustable-quantity bound is `reconciliation_locally_bound_limited`.
The last result establishes a local constrained-fit limitation, not a proof that no other solution
exists anywhere inside the permitted parameter domain. It is not reported as generic nonlinear
solver divergence, and structured diagnostics identify the active parameter, side, value, and
whether it blocks the local step.

## Caojing interpretation

For the supplied gas-turbine reports, the recommended evidence sequence is:

1. reproduce the ISO/GB-T report arithmetic and correction chain;
2. run a forward gas-path calculation with measured boundaries and compare calculated power,
   temperatures, exhaust flow and heat rate;
3. run reconciliation separately when investigating the engineer's fixed-power/relaxed-flow
   question;
4. retain discrepancies caused by missing cooling/bleed topology, loss definitions, maps or
   inconsistent measurement boundaries instead of hiding them in an unowned correction.

The reports contain steady 30-minute averages. They support steady performance analysis and
reconciliation, but not transient-model validation without synchronized time histories.

## Public example

`examples/data_reconciliation.json` fixes compressor shaft power as the hard equality and infers
inlet airflow. The discharge temperature is evaluated separately as a held-out service result in
the regression test. The thin local adapter runs the hard-equality portion with:

```sh
thermox_cli reconcile \
  --model examples/data_reconciliation.json \
  --reconciliation fixed_power_relaxed_airflow \
  --max-iterations 6 \
  --format json
```

The same example contains two redundant power readings for weighted reconciliation:

```sh
thermox_cli reconcile \
  --model examples/data_reconciliation.json \
  --reconciliation weighted_repeated_power \
  --mode weighted-measurements \
  --max-iterations 6 \
  --format json
```

CLI and future HTTP/UI adapters call the same service contract; they do not own reconciliation
logic.
