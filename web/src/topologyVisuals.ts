import type { CatalogComponent } from './types'

export type ComponentVisualFamily =
  | 'assembly'
  | 'boundary'
  | 'compressor'
  | 'turbine'
  | 'combustor'
  | 'pump'
  | 'heat_exchanger'
  | 'valve'
  | 'transport'
  | 'junction'
  | 'vessel'
  | 'shaft'
  | 'generator'
  | 'instrument'
  | 'generic'

const domainColors: Record<string, string> = {
  fluid: '#2f8bd8',
  material: '#d96b35',
  heat: '#d74242',
  shaft: '#8b68cc',
  electrical: '#d2a62c',
  signal: '#4fa17a',
  control: '#5f7585',
  force: '#697681',
}

export function topologyDomainColor(domain: string): string {
  return domainColors[domain] ?? '#718096'
}

export function componentVisualFamily(
  component: Pick<CatalogComponent, 'kind' | 'template_kind' | 'category'>,
): ComponentVisualFamily {
  const identity = `${component.template_kind} ${component.kind} ${component.category}`
    .toLowerCase()
  if (identity.includes('assembly')) return 'assembly'
  if (/source|sink|boundary|terminal/.test(identity)) return 'boundary'
  if (/compressor/.test(identity)) return 'compressor'
  if (/turbine|expander/.test(identity)) return 'turbine'
  if (/combustor|combustion/.test(identity)) return 'combustor'
  if (/pump/.test(identity)) return 'pump'
  if (/heat[_ -]?exchanger|heat transfer/.test(identity)) {
    return 'heat_exchanger'
  }
  if (/valve|restriction|orifice|regulator/.test(identity)) return 'valve'
  if (/pipe|duct|fitting|bend|transport/.test(identity)) return 'transport'
  if (/junction|mixer|splitter|combiner/.test(identity)) return 'junction'
  if (/volume|drum|separator|receiver|storage|vessel/.test(identity)) {
    return 'vessel'
  }
  if (/generator|electrical/.test(identity)) return 'generator'
  if (/shaft|gearbox/.test(identity)) return 'shaft'
  if (/sensor|control|signal/.test(identity)) return 'instrument'
  return 'generic'
}

export function connectionLineStyle(domain: string): {
  stroke: string
  strokeDasharray?: string
} {
  return {
    stroke: topologyDomainColor(domain),
    ...(domain === 'signal' || domain === 'control'
      ? { strokeDasharray: '6 4' }
      : {}),
  }
}
