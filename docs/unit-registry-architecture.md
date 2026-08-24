# Unit registry architecture

Thermox keeps persisted model, case, and result values in canonical SI. The platform-owned
`UnitRegistry` is the single authority that translates accepted authoring units into that
representation and publishes presentation metadata through `thermox.catalog/v12`.

Each registered physical dimension declares:

- its canonical persistence unit;
- SI and engineering display units;
- zero or more accepted input units and globally unambiguous aliases.

Input normalization uses
`value_si = value * scale_to_si + offset_to_si`. Presentation uses
`value_display = value_si * scale_from_si + offset_from_si`; derivatives and differences apply
only the scale. This distinction keeps offset units such as Celsius correct for both absolute
temperatures and temperature rates.

Some dimensions are display-only. They can describe graph-native or extension-defined results
without making those symbols valid scalar-input units. Registration rejects duplicate dimensions,
invalid transforms, and aliases that conflict anywhere in the registry, so parsing cannot depend
on registration order.

## Runtime composition

The default runtime starts with the built-in registry. A native extension may add dimensions in
its `register_units` callback before `make_simulation_runtime` freezes the composed runtime.
Descriptors, transforms, and aliases participate in the deterministic catalog fingerprint, so a
change to unit semantics changes runtime identity.

Every parser used by simulation and project authoring receives a registry explicitly. A custom
composition must therefore construct `ProjectService` and `SimulationRuntime` from the same
registry. The standard application composition does this implicitly with identical default
registries.

## Adapter behavior

The service publishes the complete registry in `CatalogResponse::unit_dimensions`. The web client
uses those descriptors first for authoring hints and SI/engineering presentation. Its small
built-in table is only a startup and unavailable-catalog fallback; it is not the platform unit
authority. Unknown dimensions remain unchanged rather than receiving a guessed symbol or
conversion.

This boundary lets future RPC, GUI, and CLI adapters consume the same unit contract without
embedding physics or deployment-specific unit knowledge.
