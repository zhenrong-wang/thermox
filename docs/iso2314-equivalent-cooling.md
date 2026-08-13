# ISO 2314 equivalent cooling extraction

Thermox implements the equivalent compressor-flow treatment in GB/T 14100-2016 / ISO 2314:2009,
equations 28-31. It accounts for turbine cooling air extracted at multiple compressor stages when
forming the combustion-system energy balance and ISO turbine-inlet-temperature definition.

For compressor inlet flow `m_a1`, inlet/discharge enthalpies `h_a1` and `h_a3`, and extraction
states `(m_ex_i, h_ex_i)`, Thermox evaluates:

```text
P_COMP = m_a1 (h_a3 - h_a1) - sum_i[m_ex_i (h_a3 - h_ex_i)]
m_eq   = P_COMP / (h_a3 - h_a1)
m_d    = m_a1 / m_eq - 1
Q_ex   = (m_a1 - m_eq) h_a1
```

`m_a1 - m_eq` is a work-equivalent extraction flow. It generally is not the physical sum of the
cooling extractions because air extracted at different compressor stages has received different
amounts of compression work.

## Supported evidence paths

The versioned `thermox.iso2314_equivalent_cooling/v1` request accepts exactly one determination
path:

- `extractions`: any number of physical extraction mass-flow/enthalpy pairs;
- `compressor_power_w`: independently known actual compressor power;
- `manufacturer_md`: an equipment-provider value for the ISO relative equivalent-flow difference.

The inlet flow and inlet/discharge enthalpies are required for every path. Supplying more than one
determination path is rejected rather than silently reconciling inconsistent evidence. With an
extraction schedule, Thermox validates unique extraction IDs, non-negative flows, total extraction
not exceeding inlet flow, and extraction enthalpies lying between compressor inlet and discharge.

Run the illustrative declaration with:

```sh
./build/thermox_cli iso2314-equivalent-cooling \
  --input examples/iso2314_equivalent_cooling.json \
  --format json
```

## Interpretation boundary

This calculation supplies the equivalent cooling term for the ISO combustion balance. It is not a
cooling-system design model and does not by itself calculate turbine inlet temperature. ISO
equation 35 also requires the remaining mass and energy boundary terms, consistent gas properties,
combustion efficiency, and any external extraction, leakage, injection, or cooled-air-return data.

A result derived from compressor power or a supplied `m_d` does not identify physical cooling-flow
locations or quantities. `Q_ex` also depends on the chosen enthalpy datum and must only be combined
with other balance terms evaluated on the same reference basis.
