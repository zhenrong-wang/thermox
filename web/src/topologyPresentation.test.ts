import { describe, expect, it } from 'vitest'
import { withPlacedEntity } from './topologyPresentation'

describe('topology presentation authoring', () => {
  it('adds a dropped entity without changing existing positions or viewport', () => {
    expect(
      withPlacedEntity(
        {
          position: { x: 420, y: 180 },
          presentation: {
            schema_version: 'thermox.topology_presentation/v1',
            nodes: [{ entity_id: 'inlet', x: 40, y: 80 }],
            viewport: { x: 12, y: 20, zoom: 0.9 },
          },
        },
        'compressor',
      ),
    ).toEqual({
      schema_version: 'thermox.topology_presentation/v1',
      nodes: [
        { entity_id: 'inlet', x: 40, y: 80 },
        { entity_id: 'compressor', x: 420, y: 180 },
      ],
      viewport: { x: 12, y: 20, zoom: 0.9 },
    })
  })

  it('replaces an existing entity position rather than duplicating it', () => {
    const result = withPlacedEntity(
      {
        position: { x: 9, y: 10 },
        presentation: {
          schema_version: 'thermox.topology_presentation/v1',
          nodes: [{ entity_id: 'pump', x: 1, y: 2 }],
          viewport: { x: 0, y: 0, zoom: 1 },
        },
      },
      'pump',
    )

    expect(result.nodes).toEqual([{ entity_id: 'pump', x: 9, y: 10 }])
  })
})
