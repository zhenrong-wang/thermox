import type { CatalogUnitDimension } from './types'

export type DisplayUnitProfile = 'si' | 'engineering'

export interface DisplayValue {
  value: number
  unit: string
}

interface UnitDefinition {
  unit: string
  scale: number
  offset?: number
}

interface DimensionUnits {
  si: UnitDefinition
  engineering: UnitDefinition
}

const identity = (unit: string): UnitDefinition => ({ unit, scale: 1 })

const units: Record<string, DimensionUnits> = {
  pressure: {
    si: identity('Pa'),
    engineering: { unit: 'bar', scale: 1.0e-5 },
  },
  temperature: {
    si: identity('K'),
    engineering: { unit: '°C', scale: 1, offset: -273.15 },
  },
  mass_flow: {
    si: identity('kg/s'),
    engineering: identity('kg/s'),
  },
  power: {
    si: identity('W'),
    engineering: { unit: 'MW', scale: 1.0e-6 },
  },
  electrical_power: {
    si: identity('W'),
    engineering: { unit: 'MW', scale: 1.0e-6 },
  },
  specific_enthalpy: {
    si: identity('J/kg'),
    engineering: { unit: 'kJ/kg', scale: 1.0e-3 },
  },
  specific_internal_energy: {
    si: identity('J/kg'),
    engineering: { unit: 'kJ/kg', scale: 1.0e-3 },
  },
  specific_entropy: {
    si: identity('J/kg/K'),
    engineering: { unit: 'kJ/kg/K', scale: 1.0e-3 },
  },
  specific_heat: {
    si: identity('J/kg/K'),
    engineering: { unit: 'kJ/kg/K', scale: 1.0e-3 },
  },
  specific_heat_capacity: {
    si: identity('J/kg/K'),
    engineering: { unit: 'kJ/kg/K', scale: 1.0e-3 },
  },
  energy: {
    si: identity('J'),
    engineering: { unit: 'MJ', scale: 1.0e-6 },
  },
  time: {
    si: identity('s'),
    engineering: identity('s'),
  },
  moment_of_inertia: {
    si: identity('kg*m²'),
    engineering: identity('kg·m²'),
  },
  thermal_capacity: {
    si: identity('J/K'),
    engineering: { unit: 'MJ/K', scale: 1.0e-6 },
  },
  thermal_conductance: {
    si: identity('W/K'),
    engineering: { unit: 'kW/K', scale: 1.0e-3 },
  },
  volume: {
    si: identity('m³'),
    engineering: identity('m³'),
  },
  molar_mass: {
    si: identity('kg/mol'),
    engineering: { unit: 'g/mol', scale: 1.0e3 },
  },
  angle: {
    si: identity('rad'),
    engineering: { unit: 'deg', scale: 180 / Math.PI },
  },
  angular_speed: {
    si: identity('rad/s'),
    engineering: { unit: 'rpm', scale: 60 / (2 * Math.PI) },
  },
  density: {
    si: identity('kg/m³'),
    engineering: identity('kg/m³'),
  },
  dynamic_viscosity: {
    si: identity('Pa·s'),
    engineering: identity('Pa·s'),
  },
  thermal_conductivity: {
    si: identity('W/m/K'),
    engineering: identity('W/m/K'),
  },
  speed: {
    si: identity('m/s'),
    engineering: identity('m/s'),
  },
  mass: {
    si: identity('kg'),
    engineering: identity('kg'),
  },
  frequency: {
    si: identity('Hz'),
    engineering: identity('Hz'),
  },
  dimensionless: {
    si: identity('1'),
    engineering: identity('1'),
  },
}

export function displayValue(
  valueSi: number,
  dimension: string,
  profile: DisplayUnitProfile,
  catalogDimensions: readonly CatalogUnitDimension[] = [],
): DisplayValue {
  const catalog = catalogDimensions.find(
    (item) => item.dimension === dimension,
  )
  if (catalog) {
    const definition =
      profile === 'si'
        ? catalog.si_display
        : catalog.engineering_display
    return {
      value:
        valueSi * definition.scale_from_si +
        definition.offset_from_si,
      unit: definition.symbol,
    }
  }
  const definition = units[dimension]?.[profile]
  if (!definition) return { value: valueSi, unit: dimension }
  return {
    value: valueSi * definition.scale + (definition.offset ?? 0),
    unit: definition.unit,
  }
}

export function valueToSi(
  value: number,
  dimension: string,
  profile: DisplayUnitProfile,
  catalogDimensions: readonly CatalogUnitDimension[] = [],
): number {
  const catalog = catalogDimensions.find(
    (item) => item.dimension === dimension,
  )
  if (catalog) {
    const definition =
      profile === 'si'
        ? catalog.si_display
        : catalog.engineering_display
    return (
      (value - definition.offset_from_si) /
      definition.scale_from_si
    )
  }
  const definition = units[dimension]?.[profile]
  if (!definition) return value
  return (value - (definition.offset ?? 0)) / definition.scale
}

export function displayDeltaValue(
  valueSi: number,
  dimension: string,
  profile: DisplayUnitProfile,
  catalogDimensions: readonly CatalogUnitDimension[] = [],
): DisplayValue {
  const catalog = catalogDimensions.find(
    (item) => item.dimension === dimension,
  )
  if (catalog) {
    const definition =
      profile === 'si'
        ? catalog.si_display
        : catalog.engineering_display
    return {
      value: valueSi * definition.scale_from_si,
      unit: `${definition.symbol}/s`,
    }
  }
  const definition = units[dimension]?.[profile]
  if (!definition) return { value: valueSi, unit: `${dimension}/s` }
  return {
    value: valueSi * definition.scale,
    unit: `${definition.unit}/s`,
  }
}

export function displayUnit(
  dimension: string,
  profile: DisplayUnitProfile,
  catalogDimensions: readonly CatalogUnitDimension[] = [],
): string {
  return displayValue(
    0,
    dimension,
    profile,
    catalogDimensions,
  ).unit
}

export function dimensionForUnit(
  unit: string,
  catalogDimensions: readonly CatalogUnitDimension[] = [],
): string | undefined {
  const normalized = unit.trim()
  for (const dimension of catalogDimensions) {
    if (
      dimension.accepted_units.some(
        (accepted) =>
          accepted.symbol === normalized ||
          accepted.aliases.includes(normalized),
      )
    ) {
      return dimension.dimension
    }
  }
  for (const [dimension, profiles] of Object.entries(units)) {
    if (
      profiles.si.unit === normalized ||
      profiles.engineering.unit === normalized ||
      (dimension === 'temperature' &&
        (normalized === 'C' || normalized === 'degC')) ||
      (dimension === 'volume' &&
        (normalized === 'm3' || normalized === 'm^3'))
    ) {
      return dimension
    }
  }
  return undefined
}

export const supportedCaseUnits = [
  'Pa',
  'kPa',
  'MPa',
  'bar',
  'K',
  'degC',
  'kg/s',
  'kg/h',
  'W',
  'kW',
  'MW',
  'J/kg',
  'kJ/kg',
  'J/kg/K',
  'kJ/kg/K',
  'J',
  'kJ',
  'MJ',
  's',
  'min',
  'h',
  'kg*m2',
  'kg*m^2',
  'J/K',
  'kJ/K',
  'MJ/K',
  'W/K',
  'kW/K',
  'MW/K',
  'm3',
  'L',
  'kg',
  'kg/mol',
  'g/mol',
  'rad',
  'deg',
  'rad/s',
  'rpm',
  'Hz',
  'dimensionless',
  '%',
] as const
