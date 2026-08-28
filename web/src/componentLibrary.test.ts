import { describe, expect, it } from 'vitest'
import { componentMatchesFilter } from './componentLibrary'
import type { CatalogComponent } from './types'

const component: CatalogComponent = {
  kind: 'thermox.component.compressor.performance_map',
  version: '1.0.0',
  template_kind: 'compressor',
  display_name: 'Compressor',
  category: 'Turbomachinery',
  model_name: 'Performance map',
  system_boundary_role: 'internal',
  supports_steady: true,
  supports_transient: false,
  supported_modes: [],
  events: [],
  default_mode: '',
  ports: [
    {
      name: 'inlet',
      domain: 'fluid',
      direction: 'in',
      maximum_connections: 1,
      medium_source_port: '',
    },
    {
      name: 'shaft',
      domain: 'shaft',
      direction: 'in',
      maximum_connections: 1,
      medium_source_port: '',
    },
  ],
  parameters: [],
  artifacts: [],
}

describe('component library presentation', () => {
  it('filters service-owned physical and calculation metadata', () => {
    expect(componentMatchesFilter(component, 'compressor')).toBe(true)
    expect(componentMatchesFilter(component, 'Turbomachinery')).toBe(true)
    expect(componentMatchesFilter(component, 'Performance map')).toBe(true)
    expect(componentMatchesFilter(component, 'INTERNAL')).toBe(true)
    expect(componentMatchesFilter(component, 'inlet')).toBe(true)
    expect(componentMatchesFilter(component, 'shaft')).toBe(true)
    expect(componentMatchesFilter(component, 'reactor')).toBe(false)
  })

  it('finds registered operating modes', () => {
    const hybrid = {
      ...component,
      supported_modes: ['tracking', 'failsafe'],
      events: [],
      default_mode: 'tracking',
    }
    expect(componentMatchesFilter(hybrid, 'failsafe')).toBe(true)
  })
})
