# Staged cooled turbomachinery

Thermox represents a cooled gas turbine as ordinary graph topology rather than a machine-specific
monolithic component. The reusable building blocks are:

- staged composition-aware compressor and turbine models;
- `junction.material.splitter.fixed_fraction` for compressor extraction;
- `transport.material.frozen_pressure_ratio` for pressure-compatible cooling delivery;
- `junction.material.mixer.two_inlet` for conservative stage injection;
- `shaft.combiner.two_driver` for recursively combining stage power;
- `shaft.train.two_load` for recursively distributing power to compressor sections, accessories,
  and a generator.

`core/examples/cooled_turbine_stage_cantera.json` demonstrates the smallest useful cooled-stage
assembly. A hot-gas stream and a cooler air stream enter separately. The cooling stream is
throttled to the injection pressure, the mixer conserves every species and enthalpy flow, and the
mixed stream expands through an isentropic-efficiency turbine stage. The solved benchmark closes
105.333 kg/s of material flow and produces about 56.206 MW while closing system mass and energy
balances to numerical tolerance.

Run it with:

```sh
./build/thermox_cli solve \
  --model core/examples/cooled_turbine_stage_cantera.json \
  --case design --continuation --format json
```

## Recursive machine composition

Multiple cooled stages can be chained by connecting each turbine outlet to the next stage's hot
inlet and routing a compressor extraction through its own pressure-reducing branch. Two-driver
shaft combiners form a binary tree, so the power from any number of turbine stages can feed one
shaft train without giving the component contract variable-arity ports. Two-load shaft trains can
likewise be chained to serve any number of compressor sections and loads.

The staged topology supports both detailed and reduced-order declarations. A whole compressor or
turbine may remain one component when only overall performance is known; it may be replaced by
stage groups or individual stages when OEM maps, extraction states, and cooling destinations are
available.

## Evidence boundary

The topology does not infer unavailable cooling data. Each real calculation still requires either
physical extraction fractions and stage states or an explicitly labeled calibrated/assumed
schedule. Cooling pressure losses, injection locations, stage efficiencies, leakage, and shaft
losses remain independent inputs. ISO 2314 equivalent flow is a reporting/accounting result over
those physical extraction states; it is not a substitute for the staged topology.
