# Thermox

Thermox is a thermal balance calculation platform focused on a generic equation-oriented thermal/fluid network simulator. Gas-steam combined-cycle systems are the first full target, with small Brayton and Rankine cycles used as stepping stones.

This repository contains a cycle-agnostic C++ numerical core for steady nonlinear systems and
implicit transient DAEs, a property-independent physics bridge, production CO2 and IF97 property
modules, plus the first thermal-system schema/compiler and example models.

## Design documents

- [Research & Architecture Foundation](docs/research-and-architecture.md)
- [Schema Design Draft](docs/schema-design.md)
- [Implementation Roadmap](docs/roadmap.md)
- [Numeric Core Contracts](docs/numeric-core.md)
- [Property Packages](docs/property-packages.md)

## Current C++ numeric core

Implemented in this sprint:

- CMake-based C++20 equation-solver core library and CLI runner.
- Generic equation-system builder for declaring variables, residual equations, scaling, bounds, and initial guesses independent of any cycle type.
- Variable and residual registries for equation-system assembly metadata.
- Validated CSR matrix/pattern storage and replaceable dense/sparse linear-solver hooks.
- Fixed sparse patterns with value-only Jacobian updates, dynamic sparse assembly, and hybrid
  analytic/finite-difference derivative rows.
- Scale-invariant built-in dense/CSR direct solvers for small problems and backend testing.
- Damped Newton with dimensionless row/column scaling, bounds, recoverable model-evaluation
  failures, backtracking line search, structural matching, and iteration diagnostics.
- Implicit index-1 DAE path for `F(t, y, y_dot) = 0` with differential/algebraic variables,
  consistent initial conditions, analytic dense/sparse Jacobians, adaptive backward-Euler step
  doubling, event detection, and trajectory diagnostics.
- Component registry with a base C++ `ComponentModel` interface for source/sink, compressor, turbine, pump, and simple heat-exchanger port contracts.
- Generic model graph compiler that validates registered component port contracts, creates canonical port variables, lowers connections/fixed values/component equations into sparse equation metadata, and emits a `NonlinearProblem`.
- First compiled physical component residual slices: `compressor.gas.isentropic_efficiency` and `turbine.gas.isentropic_efficiency` now use the example `IdealGas` adapter for pressure ratio, isentropic-efficiency outlet temperature, ideal-gas enthalpy, mass continuity, and shaft power equations.

The built-in direct solvers and first-order transient integrator intentionally remain
dependency-free reference backends. Larger production models should use an external sparse
factorization backend, and higher-order/stiff transient work can be added behind the established
DAE contract. Brayton remains the first physics example.

## Repository layout

```text
core/
  example_models/       Example-only adapters/components/properties that build equation systems
  examples/             Example model/case inputs
  include/thermox/      Public cycle-agnostic C++ solver headers
  src/                  Cycle-agnostic solver implementation
  tests/                Steady, transient, and example regression tests
physics/
  include/              Property-independent physics interfaces
  src/                  Ideal-gas, CO2, and IF97 adapters
  tests/                Property, steady-solver, and transient integration tests
modules/
  properties/           Pinned, library-ready CO2 and IF97 submodules
scripts/
  verify.sh             Configure, build, test, and run example solve
```

The pinned `co2` and `water-steam-if97` repositories are production modules behind the unified
Thermox property interface.

## Prerequisites

- CMake 3.20+
- A C++20 compiler such as GCC or Clang
- POSIX shell for `scripts/verify.sh`

The two property submodules are required for the default build.

## Clone

The production build does not require reference code:

```sh
git clone git@github.com:zhenrong-wang/thermox.git
cd thermox
git submodule update --init --recursive
```

## Build

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
```

## Run tests

```sh
ctest --test-dir build --output-on-failure
```

## Run the Brayton example

Text output:

```sh
./build/core/thermox_cli solve --model core/examples/brayton_simple.json
```

JSON output:

```sh
./build/core/thermox_cli solve --model core/examples/brayton_simple.json --format json
```

Expected MVP behavior: the example converges in Newton solve and reports compressor/turbine outlet temperatures, compressor/turbine/net power, heat input, thermal efficiency, and diagnostics.

## Verify everything

```sh
./scripts/verify.sh
```

## Next steps

1. Replace the example-only ideal-gas component dependency with injected `PropertyPackage`
   instances and extend residuals to pump, heat exchanger, condenser, mixer, and splitter models.
2. Add a simple IF97 Rankine example and regression tolerances.
3. Add explicit saturation-pair and analytic property-derivative APIs.
4. Integrate a production sparse factorization backend with symbolic reuse behind the current CSR
   contract.
5. Add a higher-order BDF/IDA-style DAE backend behind the transient problem contract when
   production transient cases are introduced.
