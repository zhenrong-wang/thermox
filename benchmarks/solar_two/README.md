# Solar Two public plant validation

Solar Two is the first independent operating-plant case in the Thermox validation portfolio. It
was a 10 MWe molten-salt tower plant with a nitrate-salt receiver and storage loop, a three-part
steam generator, and a non-reheat steam turbine-generator. Sandia's final report contains eight
steady subsystem test conditions, measured storage heat losses with uncertainty, startup-energy
results, and whole-plant efficiency evidence.

The authoritative source and every currently extracted value are recorded in
`source_manifest.json`. The public PDF remains external to the repository.

## Evidence boundary

The report is strong experimental evidence, but it is not a complete simulation input deck. In
particular, Appendix M does not tabulate every salt outlet, feedwater, steam-flow, turbine-exhaust,
and loss state for all eight points. Thermox therefore will not silently infer those values and
label the outcome first-principles prediction.

The planned validation progression is:

1. evaluate published hot and cold nitrate-salt states with a registered,
   source-qualified tabulated incompressible property package;
2. reproduce the steam-generator energy balance only where independent boundaries are present;
3. calibrate permitted component parameters on a declared subset of the steady points;
4. freeze those parameters and predict held-out steady points;
5. reproduce tank/sump heat-loss or cooldown behavior with measured uncertainty;
6. retain unavailable raw states as explicit evidence gaps.

`solar_salt_states.json` implements the first state-property slice. It is deliberately an ordinary Thermox model
using `source.fluid.boundary` and the generic `coolprop_incompressible` property backend. There is
no Solar-Two-specific solver or component.

Run it with:

```sh
./build/thermox_cli solve \
  --model benchmarks/solar_two/solar_salt_states.json \
  --case published_storage_states --format text
```

The model uses `sandia_solar_salt_table`, a generic piecewise-linear incompressible provider loaded
with Table 1-1 from SAND2001-2100. That table spans 500--1100°F and therefore covers Solar Two's
290°C cold state without extrapolation. Enthalpy and entropy are analytic integrals of the
piecewise-linear heat capacity, so PT, PH, and PS flashes remain mutually consistent. The separate
`coolprop_incompressible` provider remains available but keeps its stricter 300°C NaK lower limit.

The state results enable subsequent balances but do not, by themselves,
validate the reported 107 MWh usable capacity. The report explicitly accounts for inaccessible
tank heels and sump inventory without publishing a separate active salt mass; using the total
1380-tonne inventory as active mass would overpredict capacity and would be an invalid hidden
assumption.

With the source-qualified table, the 290--554°C enthalpy rise and the full reported 1380-tonne
inventory imply 153.871 MWh, not 107 MWh. Conversely, 107 MWh implies about 959.636 tonnes of
active salt. This is an input-completeness audit, not a failed model comparison: it demonstrates
that the unpublished inactive heel/sump mass is material and must not be fitted away.
