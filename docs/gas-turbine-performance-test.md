# Gas-turbine performance-test reduction

Thermox exposes a generic `thermox.gas_turbine_performance_test/v1` service
contract for measured gas-turbine performance campaigns. It is deliberately
separate from component equations: measurement reduction and standards
evidence qualify a physical simulation, but do not belong inside the nonlinear
solver.

The service computes, in SI units:

- net generator power from gross power and declared generator-side losses;
- fuel thermal input from mass flow, LHV, and optional sensible enthalpy;
- measured efficiency and heat rate;
- corrected power and heat rate from ordered multiplicative factors;
- campaign averages across accepted runs;
- standard and expanded (`k=2`) uncertainties when all required input and
  factor uncertainties are supplied.

Every correction records whether it came from an independently derived
method, a manufacturer curve, a reported factor, or an assumption. A reported
factor can reproduce an official calculation, but the result does not claim
that Thermox independently derived the OEM correction.

## ISO 2314 qualification

The result reports individual dispositions—`passed`, `failed`,
`accepted_deviation`, `not_demonstrated`, or `not_applicable`—for:

- test boundary and reference conditions;
- recommended 30-minute run duration;
- run stability;
- availability of the unmodified test record;
- instrument calibration;
- contemporaneous fuel sampling;
- correction methods and their valid ranges;
- uncertainty analysis;
- documented deviations.

ISO 2314 Table 9 constrains the maximum departure of every reading from the
reported run mean. A report containing only standard deviations is therefore
retained as evidence but marked `not_demonstrated`; the two statistics are not
silently treated as equivalent. Annex A uncertainty is likewise emitted only
when the necessary Type A/Type B inputs are supplied.

The included example is synthetic and public:

```sh
./build/thermox_cli performance-test \
  --input examples/iso2314_performance_test.json \
  --format json
```

The CLI is only a thin wrapper around
`evaluate_gas_turbine_performance_test_json`. Service and embedded callers can
use either the JSON declaration or the typed C++ request directly.

## Evidence interpretation

This capability establishes auditable performance-test arithmetic and
standards traceability. It does not turn manufacturer correction curves into a
first-principles gas-turbine prediction. A credible real-system assessment
should keep these evidence layers distinct:

1. measured power, fuel flow, and heat-rate data reduction;
2. reproduction using declared correction factors;
3. ISO procedure and uncertainty evidence;
4. boundary-constrained physical simulation;
5. calibrated component reconstruction;
6. independent off-design or transient prediction.

Advancing from one layer to the next requires additional independent data; a
small numerical residual or agreement obtained through calibrated inputs does
not change the evidence class.
