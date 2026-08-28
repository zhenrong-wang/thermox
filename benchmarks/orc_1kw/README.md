# 1 kW R245fa ORC hardware validation

This benchmark is Thermox's first publicly reproducible, multi-operating-point whole-system
hardware validation candidate. It contains 77 complete steady operating points from a physical
R245fa organic Rankine-cycle testbed: diaphragm pump, brazed-plate evaporator, scroll expander,
brazed-plate condenser, and passive liquid receiver.

The source workbook is CC BY 4.0 and is identified exactly in `source_manifest.json`. The original
file remains under ignored `tmp/` so source acquisition and derived evidence stay visibly
separate. The benchmark must never silently turn measured internal states into predictive inputs.

`validation_contract.json` freezes the evidence ladder and the first train/holdout partition
before model fitting. Cases 1--68 contain structured charge, cooling-temperature, and
expander-speed sweeps. Cases 69--77 vary source/sink flow and combined boundaries and therefore
form the primary extrapolative holdout. Randomly mixing those rows would exaggerate confidence.

## Current status

- Source provenance, license, checksum, sheets, units, completeness, and join-key integrity are
  audited.
- Thermox's `coolprop_heos` registry is now an open-substance pure-fluid provider; `R245fa` is
  runtime validated by CoolProp rather than hard-coded into a component or benchmark.
- PT/PH/PS round trips, analytic PH derivatives, transport derivatives, saturation states, and a
  real-fluid transient volume are covered for R245fa by the normal property/platform tests.
- Measured-state thermodynamic accounting is executable across all 77 cases. Using Thermox's
  pinned CoolProp 8.0.0 path, the closed refrigerant-loop energy residual is 0.763% mean absolute
  and 1.552% maximum absolute, passing the preregistered 1%/2% accounting gate. The evaporator
  refrigerant duty is 3.580% lower than the water-side duty on average; condenser refrigerant duty
  is 3.046% higher on average. Those external-side differences remain diagnostics until water-flow
  metrology, ambient losses, and instrument uncertainty are recovered from the papers.
- Case 73 produces a slightly nonpositive pump fluid-power estimate from its measured PT pair and
  is flagged rather than corrected or removed.
- External-boundary prediction remains open. It requires generic speed-dependent pump and
  expander behavior, distributed two-phase heat-exchanger inventory, passive receiver/charge
  closure, and traceable geometry or calibrated component artifacts.

## Evidence discipline

The measured-state accounting stage can validate property reconstruction and conservation, but it
cannot establish predictive accuracy. Component calibration must use only cases 1--68. The
parameters are owned by the component or system domains declared in the contract. Cases 69--77
remain untouched until the model family and acceptance criteria are frozen.
