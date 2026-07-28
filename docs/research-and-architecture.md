# Thermal Balance Calculation Platform — Research & Architecture Foundation

_Date: 2026-07-27_

## 1. Mission

Build a production-level, generic thermal/fluid balance platform. The first reference application is a gas-steam combined-cycle power plant with gas turbine, HRSG, steam turbine, condenser, pumps, valves, feedwater systems, auxiliary components, boundary conditions, component performance maps, and characteristic equations. The platform must remain generic enough for diesel engine heat recovery systems, conventional/nuclear steam cycles, cogeneration, district heating, refrigeration/ORC, and future dynamic simulation.

The platform is organized into three primary layers:

1. **Graph layer**: user-facing topology editor and component data entry.
2. **Numeric/model compiler layer**: converts topology + component definitions + boundary conditions into a well-scaled numerical problem.
3. **Solver layer**: robust nonlinear/DAE algorithms, thermodynamic property evaluation, initialization, diagnostics, and result calculation.

---

## 2. Research summary

### 2.1 IPSEpro / IPSE GO

SimTech describes IPSE as an equation-oriented heat and mass balance environment for thermal power generation. Its key relevant patterns are:

- Graphical plant assembly from component icons.
- Support for steam plants, combined-cycle plants, gas turbine systems, and cogeneration.
- Flexible component modeling instead of fixed plant templates.
- Design and off-design models/datasets.
- Model development kit for custom components.
- Browser/cloud deployment path via IPSE GO.

Source: [SimTech thermal power generation](https://simtechnology.com/applications/thermal-power-generation)

**Implication for us:** The product should not be a fixed combined-cycle calculator. It should expose an extensible component library, user-defined models, separate design/off-design cases, and an equation-oriented backend.

### 2.2 Modelica / Modelica.Fluid

Modelica.Fluid is a free Modelica package for one-dimensional thermo-fluid flow networks of vessels, pipes, fluid machines, valves, and fittings. It explicitly decouples component equations, media models, pressure-loss correlations, and heat-transfer correlations. It supports incompressible/compressible, single/multi-substance, and multiphase media. Modelica connector semantics use potential, flow, and stream variables so connection equations are generated automatically.

Sources:

- [Modelica.Fluid docs](https://build.openmodelica.org/Documentation/Modelica.Fluid.html)
- [Modelica connector docs](https://build.openmodelica.org/Documentation/Modelica.UsersGuide.Connectors.html)

**Implication for us:** Ports must be typed by physical domain and must carry enough information to generate conservation equations automatically. Component equations should not be coupled directly to a single property backend or pressure-loss implementation.

### 2.3 TESPy

TESPy (Thermal Engineering Systems in Python) models thermal engineering networks using components, connections, and networks. It supports custom equations, custom components, custom fluid property models, incremental model building, initial value generation, optimization, and exergy analysis.

Source: [TESPy documentation](https://tespy.readthedocs.io/)

**Implication for us:** TESPy remains a useful reference for network concepts and validation workflows, but Thermox should not make Python the primary runtime for the solver kernel. From day one, the graph schema, component registry, compiled model representation, and solver ABI should target a C/C++ numerical core, with scripting bindings treated as optional tooling rather than the architecture foundation.

### 2.4 OpenMDAO

OpenMDAO provides implicit components, nonlinear solvers, line searches, and explicit partial derivative definitions. Its implicit component model is a good conceptual reference: components expose residual equations and derivatives of residuals with respect to inputs/outputs.

Sources:

- [OpenMDAO solvers](https://openmdao.org/newdocs/versions/latest/features/building_blocks/solvers/solvers.html)
- [OpenMDAO ImplicitComponent](https://openmdao.org/newdocs/versions/latest/features/core_features/working_with_components/implicit_component.html)
- [OpenMDAO implicit partial derivatives](https://openmdao.org/newdocs/versions/latest/advanced_user_guide/analytic_derivatives/partial_derivs_implicit.html)

**Implication for us:** Each component should implement residual evaluation and optionally analytic Jacobian blocks. The platform must support finite difference / automatic differentiation fallbacks but encourage analytic partials for production-grade convergence.

### 2.5 SUNDIALS

SUNDIALS provides robust ODE, DAE, and nonlinear algebraic solvers. IDA targets DAE initial value problems and supports Newton/direct and inexact Newton/Krylov methods. LLNL emphasizes modular vector/matrix abstractions, user-supplied linear solvers/preconditioners, and data-independent solver design.

Sources:

- [SUNDIALS IDA introduction](https://sundials.readthedocs.io/en/latest/ida/Introduction_link.html)
- [LLNL SUNDIALS overview](https://computing.llnl.gov/projects/sundials)

**Implication for us:** The core architecture should be formulated around residuals, sparse matrices, linear solvers, and preconditioners. Even if steady-state Newton is the first implementation, the model compiler should preserve enough structure to support dynamic DAE simulation later.

### 2.6 Property libraries and process frameworks

- CoolProp is a C++ thermophysical property library with wrappers for Python, C++, Modelica, Java, MATLAB, etc., and a high-level `PropsSI` interface.
- Cantera is useful for reacting gas mixtures, combustion, chemical kinetics, equilibrium, transport, and gas turbine fuel/air/exhaust calculations.
- IDAES is a Pyomo-based process systems engineering framework for advanced energy systems, simulation, and optimization.

Sources:

- [CoolProp docs](https://coolprop.org/)
- [Cantera docs](https://cantera.org/stable/)
- [IDAES GitHub/docs links](https://github.com/IDAES/idaes-pse)

**Implication for us:** Do not build thermodynamic properties from scratch. Wrap multiple property backends behind a stable property service interface. Water/steam can start with CoolProp/IF97; combustion gas can use Cantera or simplified gas tables first.

---

## 3. Core product principles

1. **Equation-oriented, not sequential-only.** Components contribute residual equations. Connections contribute conservation/compatibility equations. The system is solved globally.
2. **Typed multi-domain graph.** Fluid, heat, shaft/mechanical, electrical, control/signal, and chemical material ports have different connector semantics.
3. **Extensible component library.** Built-in components are versioned plugins; users can add custom equations/maps through a controlled model API.
4. **Separation of physics and numerics.** Component physics should not directly own solver choices. The numeric layer assembles residual/Jacobian structures.
5. **Traceable engineering calculations.** Every calculated value should be traceable to inputs, equations, property calls, assumptions, and convergence status.
6. **Design/off-design support from day one.** Store cases/datasets separately from topology and component definitions.
7. **Robustness over cleverness.** Scaling, units, validation, good initialization, and solver diagnostics are first-class features.
8. **Generic but anchored.** Use combined cycle as the validation target, but keep abstractions general enough for diesel, nuclear steam, CHP, and other systems.

---

## 4. Proposed architecture

```text
+--------------------------------------------------------------+
| Frontend / Graph Layer                                       |
| - Plant topology editor                                      |
| - Component forms, maps, equations, units                    |
| - Case manager, run monitor, result visualization            |
+-------------------------- API -------------------------------+
| Application Services                                         |
| - Project/version storage                                    |
| - Model validation                                           |
| - Simulation job orchestration                               |
| - Result database and reporting                              |
+--------------------------------------------------------------+
| Numeric / Model Compiler Layer                               |
| - Graph schema validation                                    |
| - Connector equation generation                              |
| - Component residual registration                            |
| - Variable indexing, units normalization                     |
| - Sparse Jacobian structure                                  |
| - Initialization and tearing hints                           |
+--------------------------------------------------------------+
| Solver Layer                                                 |
| - Steady nonlinear solves                                    |
| - DAE transient solves                                      |
| - Sparse linear solvers / preconditioners                    |
| - Scaling, homotopy, continuation, diagnostics               |
+--------------------------------------------------------------+
| Physics Services                                             |
| - Property backends: CoolProp, Cantera, REFPROP adapter      |
| - Performance map interpolation/extrapolation policies       |
| - Correlation libraries: heat transfer, pressure drop        |
+--------------------------------------------------------------+
```

### 4.1 Recommended initial technology split

- **Core numeric engine:** C/C++ from day one. The first implementation may be minimal, but the core solver, residual/Jacobian assembly, sparse data structures, property-cache interface, and component-evaluation ABI should live in a compiled numerical kernel rather than a Python runtime.
- **Numerical libraries:** design around established C/C++/Fortran-compatible libraries such as SUNDIALS for nonlinear/DAE algorithms, SuiteSparse/KLU or equivalent sparse direct solvers, BLAS/LAPACK where useful, and optional AD/code-generation tools for Jacobians.
- **Property service:** C/C++ adapter layer over CoolProp, Cantera, REFPROP where licensed, and project-specific property packages. Bindings to Python or other languages are allowed for tooling, but the production property path must be callable directly from the compiled solver.
- **Backend API:** language choice is secondary to isolation. The API may be Python, C++, Go, Rust, or another service stack, but it must submit simulations to isolated worker processes and never run long solves or untrusted custom equations inside the web request process.
- **Frontend:** TypeScript + React + a graph library such as React Flow for topology editing.
- **Persistence:** PostgreSQL for projects/cases/results metadata; object storage for maps, large result arrays, and run artifacts. For early MVP, JSON/YAML files are acceptable.
- **Job execution:** process/container-based workers from the beginning, with CPU/memory/time limits, reproducible run environments, and support for concurrent simulation jobs.

---

## 5. Domain model

### 5.1 Graph-level entities

```text
Project
  └── ModelRevision
        ├── ComponentInstance[]
        ├── PortInstance[]
        ├── Connection[]
        ├── MediumPackage[]
        ├── Case[]
        └── ValidationReport
```

### 5.2 Component instance

```json
{
  "id": "gt1.compressor",
  "type": "compressor.map_based",
  "version": "1.0.0",
  "label": "Gas Turbine Compressor",
  "ports": {
    "in": {"domain": "fluid", "medium": "air"},
    "out": {"domain": "fluid", "medium": "air"},
    "shaft": {"domain": "shaft"}
  },
  "parameters": {
    "pressure_ratio_design": 18.0,
    "eta_design": 0.88,
    "map": "artifact://maps/compressor-map.csv"
  },
  "model_options": {
    "mode": "off_design",
    "map_extrapolation": "error"
  }
}
```

### 5.3 Port variables by domain

#### Fluid port

Minimum steady-state variables:

- `m_dot`: mass flow rate, kg/s; flow variable.
- `p`: static pressure, Pa; potential-like variable for connection compatibility.
- `h`: specific enthalpy, J/kg; stream variable.
- `T`: temperature, K; property-derived result or compiled boundary specification, not a normal
  fluid-connector unknown.
- `x`: composition vector / mass fractions where needed.
- `phase` or vapor quality for steam/water if applicable.

Fluid connection equations should include:

- mass conservation: sum of signed `m_dot = 0` at junction.
- pressure compatibility or pressure-drop relation depending on connection type.
- energy mixing based on incoming streams.
- species conservation for mixture networks.

#### Heat port

- `Q_dot`: heat flow rate, W.
- `T`: boundary/interface temperature, K.

#### Shaft/mechanical port

- `W_dot` or torque/speed pair.
- `omega`: rotational speed if turbomachinery maps require it.

#### Electrical port

- `P_e`, frequency/grid boundary if needed.

#### Signal/control port

- scalar/vector values that influence component equations but do not generate conservation laws.

### 5.4 Case and boundary conditions

Topology/model definition and case data must be separate.

```json
{
  "case_id": "iso_design_100pct",
  "model_revision": "rev_2026_07_27_a",
  "mode": "steady_state_design",
  "fixed_values": {
    "ambient.outlet.m_dot": {"value": 100, "unit": "kg/s"},
    "ambient.outlet.p": {"value": 101325, "unit": "Pa"},
    "ambient.outlet.T": {"value": 288.15, "unit": "K"},
    "generator.P_e": {"value": 450, "unit": "MW"}
  },
  "initial_guesses": {
    "main_steam.p": {"value": 120, "unit": "bar"}
  },
  "solver_options": {
    "tolerance": 1e-8,
    "max_iterations": 80
  }
}
```

---

## 6. Component model API

Each component type should expose metadata, ports, parameters, variables, residual equations, and optional Jacobian information.
```cpp
struct ComponentModel {
  virtual ComponentMetadata metadata() const = 0;
  virtual std::vector<PortSpec> declare_ports() const = 0;
  virtual std::vector<ParameterSpec> declare_parameters() const = 0;
  virtual std::vector<VariableSpec> declare_variables(const ModelContext& ctx) const = 0;
  virtual void evaluate_residuals(const EvalContext& ctx, ResidualView residuals) const = 0;
  virtual void evaluate_jacobian_blocks(const EvalContext& ctx, SparseBlockWriter& jacobian) const = 0;
  virtual InitGuess initialize(const InitContext& ctx) const = 0;
  virtual std::vector<Issue> validate(const ValidationContext& ctx) const = 0;
};
```

Production requirement: component equations must declare units, expected scale, variable bounds, and differentiability assumptions.

### 6.1 Component library taxonomy

Initial combined-cycle library:

- Sources/sinks: ambient air, fuel source, exhaust stack, water makeup, electrical grid.
- Gas turbine: compressor, combustor, turbine, shaft, generator, pressure losses, control boundaries.
- HRSG: economizer, evaporator, superheater, reheater, drum/separator, attemperator, gas-side pressure drop, pinch/approach constraints.
- Steam cycle: steam turbine stages/extractions, condenser, pumps, deaerator/feedwater tank, valves, heaters.
- Heat exchangers: general UA/LMTD, epsilon-NTU, segmented exchanger.
- Pipes/fittings: pressure drop, heat loss.
- Junctions/splitters/mixers.
- Controllers/constraints: fixed load, fixed pressure, fixed temperature, drum level placeholder.

Future libraries:

- Diesel engine exhaust and jacket water heat recovery.
- Nuclear steam supply system steam generators and secondary cycle.
- Refrigeration/ORC components.
- Chemical/reactive systems via Cantera.

---

## 7. Numeric/model compiler layer

The numeric layer is the most important architectural boundary. It turns user-facing graph data into solver-ready structures.

### 7.1 Responsibilities

1. Validate topology: port compatibility, medium compatibility, connection direction, required parameters.
2. Normalize units to SI.
3. Expand compound components into primitive equations if needed.
4. Create variable registry with IDs, units, scales, bounds, fixed/free status, and initial values.
5. Generate connection equations based on connector semantics.
6. Register component residual blocks.
7. Assemble sparse residual vector `F(x, u, p) = 0`.
8. Assemble sparse Jacobian `J = dF/dx`, from analytic blocks, AD, or finite difference.
9. Analyze degrees of freedom: number of free variables vs equations, rank hints, unconnected constraints.
10. Provide initialization plan and tearing hints.

### 7.2 Equation system representation

```text
CompiledModel
  variables: VariableTable
  residuals: ResidualTable
  jacobian_pattern: SparsePattern
  evaluators: list[ResidualEvaluator]
  property_calls: PropertyCallGraph
  scaling: ScalingVector
  bounds: Bounds
  diagnostics: CompilationDiagnostics
```

### 7.3 Degrees-of-freedom checks

Before solving, report:

- unknown count.
- residual equation count.
- fixed variables.
- over/under-specified components.
- disconnected graph components.
- missing medium packages.
- missing initial guesses for high-risk states.
- structurally singular blocks where detectable.

### 7.4 Initialization strategy

Recommended staged initialization:

1. Validate and solve each connected fluid network with simplified physics.
2. Estimate pressure levels from boundary conditions and component pressure ratios/drops.
3. Estimate enthalpy/temperature from design values or property defaults.
4. Initialize turbomachinery from design point equations before map-based off-design.
5. Initialize heat exchangers with fixed effectiveness or UA approximations.
6. Solve subsystems in topological/tear order where possible.
7. Run full Newton solve.
8. If needed, use continuation: design model -> off-design model, low coupling -> full coupling.

---

## 8. Solver layer design

### 8.1 Steady-state problem

Primary solve form:

```text
Find x such that F(x; fixed_inputs, parameters) = 0
```

Recommended MVP algorithms:

- Newton-Raphson with sparse linear solve.
- Damped Newton / backtracking line search.
- Trust-region or Levenberg fallback for difficult cases.
- Residual and variable scaling.
- Bounds-aware step limiting for pressures, temperatures, mass flows, quality, and map coordinates.
- Finite-difference Jacobian initially; analytic Jacobians for core components as soon as practical.

Recommended production algorithms:

- Sparse analytic/AD Jacobian blocks.
- Newton-Krylov for large systems.
- Preconditioners exploiting graph/block structure.
- Homotopy/continuation for off-design, map equations, and phase transitions.
- Tearing/decomposition to improve initialization and diagnostics.

### 8.2 Dynamic simulation path

Implemented core DAE form:

```text
F(t, y, y_dot, z, p) = 0
```

Where:

- `y`: dynamic states, e.g., drum mass/energy, metal wall temperatures, volumes.
- `z`: algebraic variables, e.g., port pressures/enthalpies/flows.
- `p`: parameters and boundary inputs.

The core now includes a dependency-free implicit backward-Euler backend with consistent
initialization, adaptive step doubling, and event detection for index-1 DAEs. SUNDIALS IDA or an
equivalent higher-order DAE backend should still be considered for production dynamic simulation;
it can implement the same DAE callback contract while adding mature BDF order control and
Newton/Krylov methods.

### 8.3 Solver diagnostics

Every run must return structured diagnostics:

- convergence flag.
- final residual norm and scaled residual norm.
- iteration history.
- worst residuals with equation names and component references.
- variable bounds hit.
- property call failures.
- Jacobian singular/ill-conditioned hints.
- suggested fixes: missing boundary, bad initial guess, impossible pinch, out-of-map, pressure conflict.

---

## 9. Thermodynamic property strategy

### 9.1 Property service interface

```python
class PropertyPackage:
    medium_id: str

    def state_ph(self, p: float, h: float, composition=None) -> ThermoState: ...
    def state_pT(self, p: float, T: float, composition=None) -> ThermoState: ...
    def saturation_p(self, p: float) -> SaturationState: ...
    def derivatives(self, state: ThermoState, wrt: list[str]) -> dict: ...
```

### 9.2 Initial backend choices

- Water/steam: CoolProp IF97 or equivalent IF97-specialized backend.
- Ideal/semi-real air/flue gas: simple ideal gas mixture first, then Cantera for combustion/exhaust composition.
- Refrigerants/ORC: CoolProp.
- High-accuracy plant acceptance tests: optional REFPROP adapter, license-dependent.

### 9.3 Non-negotiable requirements

- Cache property calls by state/medium to reduce cost.
- Define valid state ranges and fail clearly.
- Support derivatives or derivative approximation because solver robustness depends on property smoothness.
- Isolate property backend exceptions from the global solver and report them with component/variable context.

---

## 10. Combined-cycle reference model

### 10.1 Target topology

```text
Ambient air -> Compressor -> Combustor -> Gas turbine -> HRSG gas path -> Stack
Fuel source -----------^              | shaft power -> Generator

HRSG water/steam side:
Condensate pump -> LP economizer/evaporator/superheater -> LP steam turbine/admission or deaerator
Feedwater pump -> IP/HP economizers -> drums/evaporators -> superheaters/reheater -> steam turbine
Steam turbine -> condenser -> condensate pump
```

### 10.2 Validation milestones

1. **Simple Rankine cycle**: pump, boiler, turbine, condenser, fixed efficiencies.
2. **Gas turbine simple cycle**: compressor, combustor, turbine, generator, simplified gas properties.
3. **Single-pressure HRSG coupled to gas turbine exhaust.**
4. **Combined cycle with one steam pressure level.**
5. **Multi-pressure HRSG with reheater and steam turbine extractions.**
6. **Off-design maps for compressor/turbine and HRSG part-load.**

### 10.3 Example equations

Compressor/turbine simplified equations:

```text
pressure_ratio = p_out / p_in
h_out_ideal = property_solve_isentropic(p_out, s_in)
eta_c = (h_out_ideal - h_in) / (h_out - h_in)
W_dot = m_dot * (h_out - h_in)
```

Heat exchanger energy balance:

```text
Q_hot = m_hot * (h_hot_in - h_hot_out)
Q_cold = m_cold * (h_cold_out - h_cold_in)
Q_hot - Q_cold - Q_loss = 0
Q = UA * LMTD or segmented heat-transfer residuals
```

Steam turbine simplified equations:

```text
h_out_s = property_solve_isentropic(p_out, s_in)
eta_t = (h_in - h_out) / (h_in - h_out_s)
W_dot = m_dot * (h_in - h_out) * mechanical_efficiency
```

---

## 11. Data and API design

### 11.1 External model format

Use JSON/YAML for interchange. It must be stable, versioned, and independent from frontend implementation.

Top-level schema:

```yaml
schema_version: thermox.model/v1
model:
  id: ccgt_demo
  components: []
  connections: []
  media: []
cases:
  - id: design
    fixed_values: {}
    initial_guesses: {}
```

### 11.2 Backend API endpoints

Initial API:

- `POST /models/validate` — topology/schema/DOF validation.
- `POST /models/compile` — return compiled summary and diagnostics.
- `POST /simulations` — submit solve job.
- `GET /simulations/{id}` — job state.
- `GET /simulations/{id}/results` — solved variable table and component summaries.
- `GET /component-types` — component library metadata.
- `POST /component-types/custom/validate` — validate user-defined component equations/maps.

---

## 12. Production quality requirements

### 12.1 Engineering correctness

- Unit system validation and automatic conversion.
- Physical bounds on pressure, temperature, mass flow, quality, composition.
- Medium compatibility across connections.
- Reproducible runs: store model revision, case data, solver options, backend versions, and property library versions.
- Golden benchmark tests against known cycle examples.

### 12.2 Software architecture

- Component plugin interface with semantic versioning.
- Strict JSON schemas for models, cases, maps, and results.
- Test pyramid: unit tests for components/properties, integration tests for cycles, regression tests for benchmarks.
- Clear error taxonomy: schema error, validation error, compile error, property error, solver non-convergence, numerical singularity.
- Observability: structured logs, run IDs, iteration telemetry, timing of property calls and residual blocks.

### 12.3 Security and multi-user concerns

Custom equations are risky. Production deployment should avoid arbitrary code execution in the main service. Options:

1. Safe expression DSL for algebraic equations.
2. Sandboxed plugin execution.
3. Reviewed/installed server-side plugins for high-trust deployments.

Never run untrusted user Python directly in the API process.

---

## 13. Recommended implementation roadmap

### Phase 0 — Foundation design

- Finalize schemas: model, component type, case, result.
- Define component model API.
- Define property service API.
- Build benchmark set and acceptance criteria.

### Phase 1 — C++ core steady-state MVP

- Implement a minimal C++ core library for graph-independent numerical primitives: variable table, residual table, sparse pattern, scaling vectors, bounds, and diagnostics.
- Implement model parsing/validation against the language-neutral schema, either in C++ directly or in a thin application layer that emits the compiled core representation.
- Implement damped Newton with scaling, line search, finite-difference Jacobian fallback, and a sparse linear-solver abstraction.
- Implement direct C/C++ property-package adapters for simple ideal gas and water/steam backend integration.
- Implement basic compiled component models: source, sink, pump, turbine, compressor, combustor, heat exchanger, mixer/splitter, condenser.
- Validate Rankine and simple Brayton cycles through CLI tests before adding the API/UI shell.

### Phase 2 — Combined-cycle prototype

- Add HRSG segmented heat exchanger model.
- Add gas turbine simplified/off-design model.
- Add steam turbine extractions.
- Add design/off-design case manager.
- Produce combined-cycle heat balance report.

### Phase 3 — Product shell

- Backend API and job runner.
- React graph editor and component parameter forms.
- Result visualization on topology.
- Project/model revision storage.

### Phase 4 — Production hardening

- Analytic/AD Jacobians for critical components.
- Robust initialization and homotopy.
- Benchmark regression suite.
- User-defined safe equation DSL.
- Multi-user auth, audit, job isolation, and deployment automation.

---

## 14. Key risks

1. **Convergence risk:** large thermal systems are nonlinear, ill-scaled, and often poorly initialized.
2. **Property discontinuities:** phase changes and property backend failures can break Newton iterations.
3. **DOF ambiguity:** users can easily under/over-specify models.
4. **Custom model safety:** arbitrary equations/maps must be validated and sandboxed.
5. **Map extrapolation:** gas turbine/compressor maps can produce nonphysical results outside valid regions.
6. **Frontend/backend mismatch:** graph UI must not invent semantics that the numeric compiler cannot validate.

Mitigation: start with strict schemas, simple validated components, strong diagnostics, and benchmark-driven development.

---
## 15. Repository architecture

```text
thermox/
  CMakeLists.txt
  docs/
  core/
    include/thermox/    # numerical contracts
    src/                # steady Newton and transient DAE kernel
    example_models/     # isolated examples, never a platform dependency
    examples/           # generic model documents
  physics/
    include/
    src/                # unified property-package adapters
  platform/
    include/
    src/                # model validation, registries, and graph compiler
    tests/
  service/
    include/             # transport-neutral application contracts
    src/                 # validation, simulation workflows, provenance, serialization
    tests/
  modules/
    properties/         # pinned CoolProp real-fluid implementation
  scripts/
    verify.sh
```

Dependency direction is `interfaces -> service -> platform -> physics + core`; the numerical core
does not depend on any outer layer, and examples are leaf consumers. The CLI is an interface
adapter and calls the same transport-neutral service workflows intended for future RPC and worker
adapters.
