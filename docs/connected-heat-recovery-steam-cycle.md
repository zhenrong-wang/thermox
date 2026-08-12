# Connected heat-recovery steam cycle

`benchmarks/netl_b31a/connected_steam_cycle.json` joins the segmented NETL
B31A HRSG and the multi-admission HP/IP/LP steam-turbine train in one Thermox
model declaration and one nonlinear solve. It removes the artificial main
steam, cold-reheat, hot-reheat, and LP-steam boundaries used to validate the
two earlier subsystems independently.

The connected steam path is:

```text
HP feedwater -> HP economizer/evaporator/superheater -> gland split -> HP turbine
HP exhaust -> cold-reheat equalizer + IP steam -> reheater
hot reheat + throttled HP leakage -> IP turbine
IP exhaust + HRSG LP steam -> LP turbine -> condenser boundary
```

The exhaust gas simultaneously traverses ten declared HRSG surfaces. All 28
components, connection states, three shaft powers, five exhaust-species flows,
and external balances are solved together; there is no coordinator arithmetic
between an HRSG result and a turbine result.

## Recorded design-point result

With the public B31A boundary data and existing design-point calibration:

- normalized nonlinear residual: `3.61e-12`;
- external mass-balance residual: `2.27e-13 kg/s`;
- external energy-balance residual: `0 W`;
- HP/IP/LP shaft power: `59.731 / 92.541 / 126.236 MW`;
- total steam-turbine shaft power: `278.508 MW`;
- generator power at the published 97.5 percent efficiency: `271.546 MWe`,
  versus `272 MWe` published.

The power comparison is **calibrated reproduction**, because stage
efficiencies and HRSG segment duties are design-point inputs. Numerical
closure is **internal consistency**. Neither is an independent off-design
validation.

## Physical qualification

The generic counterflow audit checks all ten assumed HRSG segments and rejects
four temperature crosses. The worst is the IP economizer at approximately
`-139.35 K`; the reheater and HP/LP economizers also fail. The model therefore
solves and conserves correctly but reports `thermal_feasibility.passed=false`.
This is expected: the public report does not provide coil ordering, surface
geometry, UA values, or segment duties sufficient to reconstruct a physically
qualified HRSG arrangement.

This benchmark validates connected topology compilation, property coupling,
mixing and splitting, pressure equalization, shaft work, conservation, and
honest feasibility reporting. It does not validate HRSG detailed design or
off-design prediction.

## Remaining combined-cycle scope

This is a connected **heat-recovery steam cycle**, not yet a complete 2x1
combined-cycle plant. The gas turbines are represented only by their measured
exhaust boundary. The condenser, feed pumps, deaerator/feedwater train,
generator, and gas-turbine equipment remain external boundaries or accounting
assumptions. Those should be added as ordinary registered components when
adequate public or OEM data is available.

Run it with:

```sh
./build/thermox_cli solve \
  --model benchmarks/netl_b31a/connected_steam_cycle.json \
  --case published_connected_cycle --continuation --format text
```
