import { Handle, Position, type NodeProps } from '@xyflow/react'
import type { CatalogPort, ComponentDefinition } from './types'

export interface TopologyNodeData extends Record<string, unknown> {
  component: ComponentDefinition
  ports: CatalogPort[]
}

const domainColors: Record<string, string> = {
  fluid: '#2f8bd8',
  material: '#d96b35',
  heat: '#d74242',
  shaft: '#8b68cc',
  electrical: '#d2a62c',
  signal: '#4fa17a',
  control: '#5f7585',
}

function portColor(domain: string): string {
  return domainColors[domain] ?? '#718096'
}

export function TopologyNode({ data, selected }: NodeProps) {
  const nodeData = data as TopologyNodeData
  const inputs = nodeData.ports.filter(
    (port) => port.direction === 'in' || port.direction === 'bidirectional',
  )
  const outputs = nodeData.ports.filter(
    (port) => port.direction === 'out' || port.direction === 'bidirectional',
  )

  return (
    <article className={`topology-node${selected ? ' is-selected' : ''}`}>
      <header>
        <span className="node-label">
          {nodeData.component.label || nodeData.component.id}
        </span>
        <span className="node-id">{nodeData.component.id}</span>
      </header>
      <div className="node-kind">{nodeData.component.kind}</div>
      <div className="node-ports">
        <div>
          {inputs.map((port, index) => (
            <div className="node-port input-port" key={`in-${port.name}`}>
              <Handle
                id={port.name}
                type="target"
                position={Position.Left}
                style={{
                  top: 64 + index * 24,
                  background: portColor(port.domain),
                }}
              />
              <span
                className="domain-dot"
                style={{ background: portColor(port.domain) }}
              />
              {port.name}
            </div>
          ))}
        </div>
        <div>
          {outputs.map((port, index) => (
            <div className="node-port output-port" key={`out-${port.name}`}>
              {port.name}
              <span
                className="domain-dot"
                style={{ background: portColor(port.domain) }}
              />
              <Handle
                id={port.name}
                type="source"
                position={Position.Right}
                style={{
                  top: 64 + index * 24,
                  background: portColor(port.domain),
                }}
              />
            </div>
          ))}
        </div>
      </div>
    </article>
  )
}
