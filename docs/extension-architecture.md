# Native Extension Architecture

Thermox supports deployment-owned native extensions at the application composition root. An
extension may register component models, connector domains, property packages, thermochemistry
packages, and immutable performance-map defaults before constructing `SimulationRuntime`. HTTP,
RPC, CLI, and UI adapters see only the resulting immutable catalog and fingerprint.

This is a C++ integration boundary, not a binary plugin ABI. Dynamic library discovery, untrusted
code loading, and a safe user equation language remain separate future capabilities.

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

## Runtime identity

Connector descriptors participate in the runtime catalog fingerprint alongside components,
properties, thermochemistry packages, and deployment-default artifacts. Catalog discovery and
execution provenance enumerate the same registered connector contracts. A change to a variable,
scale, dimension, connection kind, or contract version therefore changes runtime identity.

## Conformance path

The service regression suite assembles an external runtime containing:

- a custom `thermal_bus` connector domain;
- a custom component with ports in that domain;
- a connected graph using the injected link kind and contract;
- fixed case values expressed with ordinary Thermox units.

The test requires the extension to appear in catalog discovery, compile through standard
validation, solve through the ordinary numerical service, and publish graph-native results. This
is the minimum native-extension conformance path. Future component/property SDK packaging should
retain this same end-to-end test shape.
