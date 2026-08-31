import { describe, expect, it } from 'vitest'
import {
  componentVisualFamily,
  connectionLineStyle,
  topologyDomainColor,
} from './topologyVisuals'

function component(templateKind: string, kind = templateKind, category = '') {
  return { template_kind: templateKind, kind, category }
}

describe('generic topology visuals', () => {
  it('maps calculation models to stable physical symbol families', () => {
    expect(componentVisualFamily(component('compressor.material'))).toBe(
      'compressor',
    )
    expect(componentVisualFamily(component('expander'))).toBe('turbine')
    expect(componentVisualFamily(component('fitting.fluid.return_bend'))).toBe(
      'transport',
    )
    expect(componentVisualFamily(component('source.material'))).toBe(
      'boundary',
    )
    expect(componentVisualFamily(component('control.pi'))).toBe('instrument')
  })

  it('keeps connector-domain styling independent from component type', () => {
    expect(topologyDomainColor('material')).toBe('#d96b35')
    expect(connectionLineStyle('shaft')).toEqual({ stroke: '#8b68cc' })
    expect(connectionLineStyle('signal')).toEqual({
      stroke: '#4fa17a',
      strokeDasharray: '6 4',
    })
    expect(topologyDomainColor('custom-domain')).toBe('#718096')
  })
})
