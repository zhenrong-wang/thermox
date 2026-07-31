import { describe, expect, it } from 'vitest'
import {
  componentDisplayName,
  componentFamily,
  componentMatchesFilter,
} from './componentLibrary'
import type { CatalogComponent } from './types'

const component: CatalogComponent = {
  kind: 'thermox.component.compressor.performance_map',
  version: '1.0.0',
  system_boundary_role: 'internal',
  supports_steady: true,
  supports_transient: false,
  ports: [
    {
      name: 'inlet',
      domain: 'fluid',
      direction: 'in',
      maximum_connections: 1,
    },
    {
      name: 'shaft',
      domain: 'shaft',
      direction: 'in',
      maximum_connections: 1,
    },
  ],
  parameters: [],
  artifacts: [],
}

describe('component library presentation', () => {
  it('derives readable names without owning the component inventory', () => {
    expect(componentDisplayName(component.kind)).toBe('performance_map')
    expect(componentFamily(component.kind)).toBe('thermox.component')
  })

  it('filters registry descriptors by kind, role, port, and domain', () => {
    expect(componentMatchesFilter(component, 'compressor')).toBe(true)
    expect(componentMatchesFilter(component, 'INTERNAL')).toBe(true)
    expect(componentMatchesFilter(component, 'inlet')).toBe(true)
    expect(componentMatchesFilter(component, 'shaft')).toBe(true)
    expect(componentMatchesFilter(component, 'reactor')).toBe(false)
  })
})
