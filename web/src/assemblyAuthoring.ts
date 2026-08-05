import type {
  AssemblyDefinition,
  CatalogComponent,
  CatalogPort,
} from './types'

function endpoint(value: string): [string, string] {
  const separator = value.lastIndexOf('.')
  return separator === -1
    ? [value, '']
    : [value.slice(0, separator), value.slice(separator + 1)]
}

function resolvePort(
  assembly: AssemblyDefinition,
  endpointValue: string,
  catalog: Map<string, CatalogComponent>,
): CatalogPort | undefined {
  const [childId, portName] = endpoint(endpointValue)
  const component = assembly.components.find((item) => item.id === childId)
  if (component) {
    return catalog
      .get(component.kind)
      ?.ports.find((port) => port.name === portName)
  }
  const nested = (assembly.assemblies ?? []).find(
    (item) => item.id === childId,
  )
  const exported = nested?.ports.find((port) => port.name === portName)
  return nested && exported
    ? resolvePort(nested, exported.endpoint, catalog)
    : undefined
}

export function assemblyPorts(
  assembly: AssemblyDefinition,
  catalog: Map<string, CatalogComponent>,
): CatalogPort[] {
  return assembly.ports.flatMap((exported) => {
    const resolved = resolvePort(assembly, exported.endpoint, catalog)
    return resolved ? [{ ...resolved, name: exported.name }] : []
  })
}
