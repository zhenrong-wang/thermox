import { describe, expect, it } from 'vitest'
import { initialTopologyDocument } from './topologyAuthoring'
import type { Project } from './types'

describe('initialTopologyDocument', () => {
  it('creates an empty, authorable topology for a project', () => {
    const project = {
      name: 'Combined Cycle Study',
    } as Project
    expect(initialTopologyDocument(project)).toEqual({
      schema_version: 'thermox.topology/v1',
      model: {
        id: 'combined_cycle_study',
        name: 'Combined Cycle Study',
        revision: '1',
        media: [],
        materials: [],
        components: [],
        connections: [],
      },
    })
  })
})
