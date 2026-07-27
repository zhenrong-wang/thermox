# Thermox

Thermox is a system-agnostic, equation-oriented thermal/fluid simulation platform. A system is
defined by registered components, registered fluid-property packages, topology, and operating
cases—not by a hard-coded cycle type. Gas turbines, steam plants, combined cycles, nuclear heat
systems, refrigeration systems, and other networks use the same platform path.

This repository contains a cycle-agnostic C++ numerical core for steady nonlinear systems and
implicit transient DAEs, a property-independent physics bridge, production CO2 and IF97 property
modules, a generic thermal-system schema/compiler platform, and isolated example models.

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
- Generic DAE equation-system builder for differential/algebraic variables, accumulation equations,
  derivative scaling, bounds, checked evaluation, and fixed sparse Jacobians.
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
- First-class `thermox_platform` module with a model document, component registry, property registry
  integration, and graph compiler.
- Base C++ `ComponentModel` interface with physical compressor, turbine, pump, valve, fixed-duty
  and counterflow-UA two-stream heat exchangers, quality-target evaporator/condenser, mixer,
  splitter, lumped thermal storage, and rigid adiabatic fluid volume implementations.
- Queryable component catalog descriptors for ports, simulation modes, property capabilities,
  parameters, SI dimensions, defaults, and open/closed bounds. The compiler rejects missing,
  unknown, dimensionally incompatible, and out-of-range component parameters before equation
  assembly.
- Generic model graph compiler that validates registered component port contracts, creates canonical port variables, lowers connections/fixed values/component equations into sparse equation metadata, and emits a `NonlinearProblem`.
- Fluid connectors use conserved primary unknowns (`m_dot`, `p`, and `h`); temperature, entropy,
  density, phase, and quality are derived through the selected property package after solving.
- Natural temperature boundary specifications compile into PH property equations without adding
  redundant temperature unknowns.
- Compile-time degree-of-freedom validation rejects under- and over-specified models with variable
  and equation counts before Newton is invoked.
- Transient graph compiler that validates component simulation-mode support, creates internal
  component states, lowers accumulation and algebraic equations, and emits a `DaeProblem`.
- Property-aware compressor, turbine, and pump residuals declare their required flash capabilities,
  resolve their medium backend from the model,
  use PH/PS flashes for isentropic efficiency, propagate recoverable property-domain failures to
  Newton, and support ideal gas and real fluids without changing component equations.

The built-in direct solvers and first-order transient integrator intentionally remain
dependency-free reference backends. Larger production models should use an external sparse
factorization backend, and higher-order/stiff transient work can be added behind the established
DAE contract. Cycle examples do not define or constrain the platform API.

## Repository layout

```text
core/
  example_models/       Isolated example calculations; not a platform dependency
  examples/             Example model/case inputs
  include/thermox/      Public cycle-agnostic C++ solver headers
  src/                  Cycle-agnostic solver implementation
  tests/                Steady and transient numeric-kernel tests
platform/
  include/              Generic model-document and component-registry API
  src/                  Boundary, storage, turbomachinery, and transport component modules;
                        catalog validation; and graph compilation
  tests/                System-agnostic platform and property-integration tests
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

## Run a generic model

Text output:

```sh
./build/thermox_cli solve --model core/examples/air_compressor.json
```

JSON output:

```sh
./build/thermox_cli solve --model core/examples/air_compressor.json --format json
```

The CLI parses the generic model document, resolves the registered component and fluid backend,
compiles the graph to a nonlinear problem, solves it, and reports primary variables plus derived
fluid-port properties.

Run a generic transient model:

```sh
./build/thermox_cli simulate \
  --model core/examples/lumped_thermal_storage.json \
  --case charge \
  --end-time 10 \
  --format json
```

The transient example uses a registered lumped thermal-storage component with an internal
differential temperature state. The platform performs consistent DAE initialization before
adaptive integration.

The transient component path also supports a property-backed rigid fluid volume. Its mass and
total internal energy are differential states, while pressure and enthalpy are algebraic states
closed by the selected fluid package. The same component therefore works with any registered
backend that supplies a PH flash.

Run the IF97 Rankine regression:

```sh
./build/thermox_cli solve \
  --model core/examples/simple_rankine.json \
  --case design \
  --format json
```

This example is compiled as a normal component graph. The cycle is cut at the condensate boundary
to avoid redundant closed-loop continuity rows; the regression reports and bounds the resulting
cut-stream energy mismatch explicitly.

## Verify everything

```sh
./scripts/verify.sh
```

## Next steps

1. Extract heat-transfer/phase-change and fluid-inventory models into independent
   component-family registrars.
2. Add explicit saturation-pair property APIs and exact saturated-liquid/vapor component targets.
3. Add wall thermal mass, rotating inertia, and control components using the established
   transient component contract.
4. Add closed-loop equation reduction for redundant stream continuity constraints.
5. Add analytic property-derivative APIs; the rigid volume currently
   computes local PH closure derivatives through bounded property calls.
6. Integrate a production sparse factorization backend with symbolic reuse behind the current CSR
   contract.
7. Add a higher-order BDF/IDA-style DAE backend behind the transient problem contract when
   production transient cases are introduced.
