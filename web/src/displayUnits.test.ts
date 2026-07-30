import { describe, expect, it } from 'vitest'
import {
  dimensionForUnit,
  displayDeltaValue,
  displayValue,
  valueToSi,
} from './displayUnits'

describe('display units', () => {
  it('round-trips scaled and offset engineering units', () => {
    const pressure = displayValue(2.5e6, 'pressure', 'engineering')
    expect(pressure.unit).toBe('bar')
    expect(pressure.value).toBeCloseTo(25)
    expect(valueToSi(pressure.value, 'pressure', 'engineering')).toBe(2.5e6)

    const temperature = displayValue(300, 'temperature', 'engineering')
    expect(temperature.unit).toBe('°C')
    expect(temperature.value).toBeCloseTo(26.85)
    expect(
      valueToSi(temperature.value, 'temperature', 'engineering'),
    ).toBeCloseTo(300)
    expect(displayDeltaValue(2, 'temperature', 'engineering')).toEqual({
      value: 2,
      unit: '°C/s',
    })
  })

  it('preserves extension-defined dimensions without guessing', () => {
    expect(displayValue(42, 'custom_flux', 'engineering')).toEqual({
      value: 42,
      unit: 'custom_flux',
    })
    expect(valueToSi(42, 'custom_flux', 'engineering')).toBe(42)
  })

  it('prefers catalog-published extension dimensions', () => {
    const catalog = [
      {
        dimension: 'custom_flux',
        canonical_unit: 'flux_si',
        si_display: {
          symbol: 'flux_si',
          scale_from_si: 1,
          offset_from_si: 0,
        },
        engineering_display: {
          symbol: 'kflux',
          scale_from_si: 0.001,
          offset_from_si: 0,
        },
        accepted_units: [
          {
            symbol: 'kflux',
            aliases: ['custom-kflux'],
            scale_to_si: 1000,
            offset_to_si: 0,
          },
        ],
      },
    ]
    expect(
      displayValue(42000, 'custom_flux', 'engineering', catalog),
    ).toEqual({ value: 42, unit: 'kflux' })
    expect(
      valueToSi(42, 'custom_flux', 'engineering', catalog),
    ).toBe(42000)
    expect(dimensionForUnit('custom-kflux', catalog)).toBe(
      'custom_flux',
    )
  })

  it('recognizes canonical case units', () => {
    expect(dimensionForUnit('bar')).toBe('pressure')
    expect(dimensionForUnit('degC')).toBe('temperature')
    expect(dimensionForUnit('m3')).toBe('volume')
  })
})
