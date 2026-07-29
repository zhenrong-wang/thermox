# Thermox

Thermox is a system-agnostic, equation-oriented thermal/fluid simulation platform. A system is
defined by registered components, registered fluid-property packages, topology, and operating
cases—not by a hard-coded cycle type. Gas turbines, steam plants, combined cycles, nuclear heat
systems, refrigeration systems, and other networks use the same platform path.

This repository contains a cycle-agnostic C++ numerical core for steady nonlinear systems and
implicit transient DAEs, a property-independent physics bridge, production CO2 and water/steam
property adapters backed by CoolProp, a generic thermal-system schema/compiler platform, and
isolated example models.

## Design documents

- [Research & Architecture Foundation](docs/research-and-architecture.md)
- [Schema Design Draft](docs/schema-design.md)
- [Implementation Roadmap](docs/roadmap.md)
- [Numeric Core Contracts](docs/numeric-core.md)
- [Property Packages](docs/property-packages.md)
- [Service Architecture](docs/service-architecture.md)
- [Graph Platform Architecture Review](docs/graph-platform-review.md)
- [Persistence Architecture](docs/persistence-architecture.md)
- [Performance Map Architecture](docs/performance-maps.md)
- [Calibration Architecture](docs/calibration-architecture.md)

## Current C++ numeric core

Implemented in this sprint:

- CMake-based C++20 equation-solver core library and CLI runner.
- Generic equation-system builder for declaring variables, residual equations, scaling, bounds, and initial guesses independent of any cycle type.
- Generic DAE equation-system builder for differential/algebraic variables, accumulation equations,
  derivative scaling, bounds, checked evaluation, and fixed sparse Jacobians.
- Variable and residual registries for equation-system assembly metadata.
- Validated CSR matrix/pattern storage and replaceable dense/sparse linear-solver hooks.
- Optional SuiteSparse UMFPACK sparse LU backend for large, strongly scaled plant graphs; the
  dependency-free sparse elimination remains available for standalone/reference builds.
- Fixed sparse patterns with value-only Jacobian updates, dynamic sparse assembly, and hybrid
  analytic/finite-difference derivative rows.
- Composition-preserving material splitters and enthalpy-conserving material mixers for bleed,
  cooling, recirculation, and multistream reacting-system graphs.
- Fixed-composition material sources use species-keyed, bounds-checked mass fractions while
  leaving total flow available for the connected equipment graph to solve.
- Performance-map compressor and turbine models for both ordinary fluid and composition-aware
  material gas paths, with component-owned flow-capacity, pressure-ratio, and efficiency
  corrections that can be calibrated without modifying immutable map artifacts.
- Conditioned three-coordinate map families for variable-geometry turbomachinery, with per-case
  angle selection across complete corrected-flow/speed maps.
- Typed per-case component-parameter overrides for operating controls and configuration changes
  without duplicating system topology.
- Scale-invariant built-in dense/CSR direct solvers for small problems and backend testing.
- Damped Newton with dimensionless row/column scaling, bounds, recoverable model-evaluation
  failures, backtracking line search, structural matching, and iteration diagnostics.
- Implicit index-1 DAE path for `F(t, y, y_dot) = 0` with differential/algebraic variables,
  consistent initial conditions, analytic dense/sparse Jacobians, adaptive backward-Euler step
  doubling, event detection, and trajectory diagnostics.
- First-class `thermox_platform` module with a model document, component registry, property registry
  integration, and graph compiler.
- A separate composition-aware thermochemistry contract validates ordered species bases and
  mass/mole fractions, exposes PT/PH/equilibrium-HP capabilities, and supports mechanism-specific
  backends without coupling components directly to a chemistry library.
- CoolProp-backed ambient moist-air conversion supplies humidity ratio, water mass fraction, and
  humid-air bulk properties from measured pressure, dry-bulb temperature, and relative humidity.
- First-class `thermox_service` application module with versioned validate, steady, and transient
  commands; an injectable immutable runtime; component/property/connector catalog discovery;
  compile-aware validation; structured diagnostics; exact version-pin enforcement; and canonical
  `thermox.result/v3` JSON with complete execution provenance and graph-native steady/transient
  values. Its `thermox.job/v2` workflow adds Team-scoped idempotent execution, atomic worker claims,
  optimistic job revisions, terminal states, checksummed external result artifacts, stable job
  status JSON, and service-owned result retrieval for thin RPC adapters.
- A separate framework-neutral `thermox_http_api` adapter maps health, catalog, compile-aware
  validation, steady, and transient HTTP routes onto `thermox_service`, with strict query decoding,
  JSON content checks, body limits, transport status codes, and safe response headers. A thin
  Boost.Beast host publishes the same adapter locally without owning simulation logic.
- Asynchronous HTTP simulations carry a gateway-supplied identity context, namespace idempotency
  and resource access by Team, retain the submitting user for audit, and hide cross-Team job
  existence. The local host injects an explicit local identity; authentication is not yet
  implemented.
- Base C++ `ComponentModel` interface with physical compressor, turbine, pump, valve, fixed-duty
  and counterflow-UA two-stream heat exchangers, quality-target evaporator/condenser, mixer,
  splitter, lumped thermal storage, and rigid adiabatic fluid volume implementations.
- A map-driven compressor component resolves an instance-bound performance artifact, evaluates
  corrected mass flow and shaft speed, and closes pressure ratio, efficiency, and shaft power for
  off-design operating points.
- The corresponding map-driven turbine uses the same domain-neutral artifact format with
  turbine-specific expansion, efficiency, and generated shaft-power conventions.
- Typed shaft-train and generator components balance one driver against two loads, mechanical
  efficiency, fixed losses, electrical conversion efficiency, and speed/frequency conversion.
  Electrical power and frequency use their own connector contract.
- Queryable component catalog descriptors for ports, simulation modes, property capabilities,
  parameters, SI dimensions, defaults, and open/closed bounds. The compiler rejects missing,
  unknown, dimensionally incompatible, and out-of-range component parameters before equation
  assembly.
- Generic dimensioned performance-map interpolation across non-rectangular curve families, with
  analytic piecewise derivatives and explicit reject, clamp, or linear extrapolation policies.
- Versioned, checksummed performance-map artifacts bound to component instances by generic
  artifact roles and resolved from isolated per-request bundles over optional immutable deployment
  defaults during graph compilation. Bundles accept inline data or immutable references through an
  injectable resolver; equipment datasets are not embedded in component types or the numeric
  kernel, and queued runs preserve their payload identity and provenance.
- Generic model graph compiler that validates registered component port contracts, creates canonical port variables, lowers connections/fixed values/component equations into sparse equation metadata, and emits a `NonlinearProblem`.
- Topology-aware steady boundary audits aggregate mass and enthalpy flow for fluid/material streams
  and energy transfer for heat, shaft, and electrical ports, with explicit source/sink semantics
  discoverable through the component catalog. Per-component net-flow metrics attribute conversion
  losses to the equipment that owns them.
- `thermox.model/v2` component instances bind media to fluid ports while all port names, domains,
  directions, and connection cardinality come from the active runtime catalog.
- Calibration campaigns select explicit component/connection parameter targets without changing
  their physical ownership, and carry component/system sharing scope, cases, bounds, priors,
  measured graph observations, and uncertainties through canonical service serialization.
- Service validation resolves calibration observations against registered graph results, while
  the result layer derives thermodynamic temperature and composition metadata for reacting
  material ports through the selected thermochemistry backend.
- A bounded multi-case calibration service performs sequential uncertainty-weighted coordinate
  search through ordinary steady simulations and returns fitted parameters, residual attribution,
  execution provenance, and a reusable canonical fitted model.
- A leakage-guarded engineering-study service calibrates designated baseline cases, freezes the
  canonical fitted model, runs independent steady prediction cases, and reports per-observation
  and aggregate normalized validation residuals.
- Composition-aware compressor and turbine residuals cache thermochemistry flashes by their local
  pressure, enthalpy, and species-flow dependencies, preventing unrelated graph perturbations from
  repeatedly invoking expensive backend calculations.
- Deterministic closed-loop reduction omits only compiler-generated connection rows proven to be
  consistent linear combinations of retained equations; reduced row names remain available as
  graph diagnostics.
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
  src/                  Modular physical component families, catalog validation, and graph
                        compilation
  tests/                System-agnostic platform and property-integration tests
service/
  include/              Transport-neutral commands, jobs, repository ports, and result contracts
  src/                  Simulation/job orchestration, serialization, and local adapters
  tests/                Service-boundary, repository, and workflow tests
physics/
  include/              Property-independent physics interfaces
  src/                  Ideal-gas and CoolProp-backed CO2/IF97 adapters
  tests/                Property, steady-solver, and transient integration tests
modules/
  properties/           Pinned CoolProp dependency
                        and optional pinned Cantera source
scripts/
  verify.sh             Configure, build, test, and run example solve
```

CoolProp 8.0.0 is pinned as the sole real-fluid implementation behind the unified Thermox property
interface. CO2 uses CoolProp HEOS/Span-Wagner, while water and steam use CoolProp IF97.
The former in-house CO2 and water/steam submodules are not built or retained.

## Prerequisites

- CMake 3.20+
- A C++20 compiler such as GCC or Clang
- POSIX shell for `scripts/verify.sh`

The CoolProp submodule is required for the default build. Its pinned build dependencies are
resolved by CoolProp through CPM during configuration and can be shared through `CPM_SOURCE_CACHE`.

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
cmake --build build --parallel 2
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

The CLI is a thin terminal adapter. It reads arguments and model text, calls `thermox_service`, and
renders the returned contract. Model parsing, registry resolution, graph compilation, solving,
derived-property evaluation, provenance, and JSON result construction belong to the service.

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

This example is compiled as a true closed component graph. The compiler detects and omits the
redundant mass-flow and pressure connection rows at the loop closure while retaining enthalpy
closure. Mass flow and base pressure are normal case specifications.

## Verify everything

```sh
./scripts/verify.sh
```

Verification uses two build jobs by default so the pinned CoolProp build does not monopolize a
development host. A machine with more available capacity can opt in explicitly, for example:

```sh
THERMOX_BUILD_JOBS=4 ./scripts/verify.sh
```

## Next steps

1. Map thin HTTP/RPC endpoints onto the synchronous and job application services, then add
   PostgreSQL and object-storage adapters behind the established repository ports.
2. Add wall thermal mass, rotating inertia, and control components using the established
   transient component contract.
3. Add analytic property-derivative APIs; the rigid volume currently
   computes local PH closure derivatives through bounded property calls.
4. Integrate a production sparse factorization backend with symbolic reuse behind the current CSR
   contract.
5. Add a higher-order BDF/IDA-style DAE backend behind the transient problem contract when
   production transient cases are introduced.
