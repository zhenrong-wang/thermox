import type { Project, TopologyDocument } from './types'

function identifier(value: string): string {
  return (
    value
      .trim()
      .toLowerCase()
      .replace(/[^a-z0-9_-]+/g, '_')
      .replace(/^_+|_+$/g, '') || 'thermal_system'
  )
}

export function initialTopologyDocument(project: Project): TopologyDocument {
  return {
    schema_version: 'thermox.topology/v1',
    model: {
      id: identifier(project.name),
      name: project.name,
      revision: '1',
      media: [],
      materials: [],
      components: [],
      connections: [],
    },
  }
}
