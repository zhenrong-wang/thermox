# Native Extension Architecture

Thermox supports deployment-owned native extensions at the application composition root. An
extension may register component models, connector domains, property packages, thermochemistry
packages, unit dimensions, and immutable performance-map defaults before constructing
`SimulationRuntime`. HTTP, RPC, CLI, and UI adapters see only the resulting immutable catalog and
fingerprint.

This is a C++ integration boundary, not a binary plugin ABI. Dynamic library discovery and
untrusted code loading remain separate future capabilities; the constrained declarative equation
language below is the non-executable user extension path.

The safe equation-language boundary is available for trusted deployment composition through
`thermox.expression_component/v5`. It registers declarative algebraic/DAE components, including
fixed-topology operating modes and checked component-owned event/reset maps, derives sparse
analytic rows, and executes through the ordinary component registry without loading code.
Its p-h property functions derive a catalog-visible `state_ph` requirement and override
`requires_property_capability_on_port` so compilation checks only the fluid ports actually used by
those calls. Native models inherit the conservative default that applies every descriptor-level
property requirement to every bound fluid port and may use the same hook when their requirements
are port-specific.
Persisted user authoring and approval remain separate from the native extension boundary. See
`docs/custom-expression-components.md`.

## SDK package

An installed Thermox SDK provides these CMake targets:

- `thermox::core`
- `thermox::physics`
- `thermox::platform`
- `thermox::service`
- `thermox::service_native`

Native extensions normally include `thermox/service/native_extension_sdk.hpp` and link
`thermox::service_native`. `NativeExtensionPackage` gives each source-level package an ID, version,
and optional registration callbacks for components/connectors, properties, engineering artifacts,
correlation templates, units, and thermochemistry. `apply_native_extension` validates the package
envelope and applies its callbacks before `make_simulation_runtime` freezes the composed
registries. A successfully
applied package retains its ID and version in catalog discovery; duplicate package IDs are
rejected.

CoolProp remains a private runtime archive in the installed package; its build-tree headers and
third-party implementation details are not exported through the SDK. Optional Cantera-enabled SDK
builds retain Cantera as an ordinary discoverable external dependency.

## Connector domains

`ComponentRegistry` owns the connector-domain authority used by its components. Every domain
descriptor declares:

- a unique domain name;
- a versioned connector contract;
- the connection kind accepted by graph validation;
- canonical variables with initial values, numerical scales, and SI dimensions;
- whether a variable is expanded over a material species basis.

The registry validates uniqueness, finite initial values, positive finite scales, dimensions, and
species placeholders. Component registration rejects ports that name an unregistered domain.
Graph compilation resolves variables and connection semantics from this registry rather than from
a hard-coded domain switch.

The standard runtime registers fluid, material, heat, shaft, electrical, signal, and control
domains. Fluid medium compatibility and material/species compatibility remain deliberate physics
rules above the generic connector registry. Custom domains receive generic equality connections
and graph-native primary results; domain-specific balance or derived-property evaluators can be
added independently.

Connector equality rows are also registered as initialization relations. An explicit case value
on either endpoint therefore seeds the other endpoint while preserving values explicitly supplied
on both sides. Native components may add their own linear initialization relations through
`EquationSystemBuilder`, but this is opt-in and should only be used when the relation is safe for
the component's entire target and continuation contract.

## Runtime identity

Connector, correlation-template, and unit descriptors plus native-extension package identities
participate in the runtime catalog fingerprint alongside components, properties, thermochemistry
packages, and
deployment-default artifacts. Catalog discovery and execution provenance enumerate the same
registered contracts. A change to an extension version, connector variable, unit transform or
alias, correlation equation or bounds, scale, dimension, connection kind, or contract version
therefore changes runtime identity.

## Conformance path

`sdk/conformance` is a separate CMake consumer project. The
`thermox_sdk_conformance` target stages an install, configures that project using only
`find_package(Thermox)`, builds it serially, and runs it. The fixture assembles an external runtime
containing:

- a custom `thermal_bus` connector domain;
- a custom component with ports in that domain;
- a custom property backend;
- a custom correlation template;
- a custom unit dimension and accepted alias;
- a connected graph using the injected link kind and contract;
- fixed case values expressed with built-in and extension-defined units.

The test requires the extension to appear in catalog discovery, compile through standard
validation, solve through the ordinary numerical service, and publish graph-native results. This
is the minimum native-extension conformance path.

## Derivative conformance

Component extensions may provide sparse equation derivatives through `EquationSystemBuilder`.
Thermox uses those rows directly and fills only unprovided rows by finite differences. Extension
tests should call `verify_problem_jacobian` on a compiled problem at representative operating
points before relying on those derivatives in production. The verifier compares every entry of
each provider-owned row—including expected zeros—against bounded, scale-aware finite differences
and reports equation/variable names for mismatches. Rows intentionally owned by the numerical
fallback are excluded, so failures identify the extension's derivative contract rather than the
solver's fallback implementation.
