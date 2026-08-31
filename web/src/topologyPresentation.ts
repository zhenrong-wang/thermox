import type { Node, Viewport, XYPosition } from '@xyflow/react'
import type { TopologyPresentation } from './types'

export interface CanvasComponentPlacement {
  position: XYPosition
  presentation: TopologyPresentation
}

export function presentationFromNodes(
  nodes: ReadonlyArray<Pick<Node, 'id' | 'position'>>,
  viewport: Viewport,
): TopologyPresentation {
  return {
    schema_version: 'thermox.topology_presentation/v1',
    nodes: nodes.map((node) => ({
      entity_id: node.id,
      x: node.position.x,
      y: node.position.y,
    })),
    viewport,
  }
}

export function withPlacedEntity(
  placement: CanvasComponentPlacement,
  entityId: string,
): TopologyPresentation {
  return {
    ...placement.presentation,
    nodes: [
      ...placement.presentation.nodes.filter(
        (node) => node.entity_id !== entityId,
      ),
      {
        entity_id: entityId,
        x: placement.position.x,
        y: placement.position.y,
      },
    ],
  }
}
