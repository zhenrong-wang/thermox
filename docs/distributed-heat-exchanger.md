# Distributed heat exchanger

`core/examples/two_cell_counterflow_heat_exchanger.json` builds a distributed gas-to-water heat
exchanger from two ordinary `heat_exchanger.fluid.dynamic_cell` instances. The platform contains no
special two-cell or HRSG solver path.

## Topology

The hot path follows the cell numbering:

```text
hot source -> cell 1 hot side -> cell 2 hot side -> hot sink
```

The cold path is connected in reverse order:

```text
cold source -> cell 2 cold side -> cell 1 cold side -> cold sink
```

Reversing the cold-side cell order is what makes the discretization counterflow. A co-current model
would connect both streams through the cells in the same order. More cells increase spatial
resolution without changing the component implementation or numerical interfaces.

The example binds ideal-gas air to both hot-side ports and IF97 water to both cold-side ports. Each
cell owns its fluid holdup values, hot- and cold-side conductances, wall thermal capacitance, flow
diameters, and loss coefficients.

## Run the steady case

```sh
./build/thermox_cli solve \
  --model core/examples/two_cell_counterflow_heat_exchanger.json \
  --case steady \
  --format json
```

The reference solution transfers approximately 102.4 kW. Hot- and cold-side duties agree to solver
tolerance, and pressure falls monotonically through both cells on both flow paths.

## Run the transient case

```sh
./build/thermox_cli simulate \
  --model core/examples/two_cell_counterflow_heat_exchanger.json \
  --case heat_up \
  --end-time 0.5 \
  --format json
```

Both hot-fluid holdups cool, both water holdups warm, and both wall temperatures evolve. The
regression sums the derivatives of all four stored fluid energies and both wall energies. That sum
equals the net enthalpy flow through the four system boundaries; heat transferred between cells and
walls cancels internally.

## Engineering interpretation

This example proves graph composition and conservation, not discretization convergence for a
particular exchanger. A real model must select the number of cells and assign geometry,
conductances, loss data, holdups, fouling assumptions, and boundary conditions from engineering
information. Cell-count sensitivity should be treated as a model verification study and persisted
as separate Study revisions.

The same topology pattern can represent recuperators and single-phase economizer or superheater
sections. Two-phase void fraction, circulation, moving boundaries, and flow-regime correlations
require additional registered calculation models; they are not inferred from this example.
