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

The current service contract reuses a named model calibration declaration only as a typed list of
adjustable targets and observations. Reconciliation gives those entries stricter semantics:

- every observation is a hard equality;
- the number of adjustable quantities must equal the number of hard equalities;
- all adjustable quantities require finite bounds;
- priors are rejected because they would turn an equality solve into a compromise objective;
- a finite-difference sensitivity matrix drives a bounded, line-searched Newton iteration;
- a singular matrix is reported as unidentifiable rather than regularized silently;
- success requires every normalized hard residual to meet the declared tolerance.

The observation `sigma` acts only as a numerical residual scale in this intent. It does not soften
the equality.

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

CLI and future HTTP/UI adapters call the same service contract; they do not own reconciliation
logic.
