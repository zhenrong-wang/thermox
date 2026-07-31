import type { CatalogComponent } from './types'

export const COMPONENT_DRAG_TYPE =
  'application/vnd.thermox.component-kind'

export function componentMatchesFilter(
  component: CatalogComponent,
  rawQuery: string,
): boolean {
  const query = rawQuery.trim().toLowerCase()
  if (!query) return true

  return (
    component.kind.toLowerCase().includes(query) ||
    component.system_boundary_role.toLowerCase().includes(query) ||
    component.ports.some(
      (port) =>
        port.name.toLowerCase().includes(query) ||
        port.domain.toLowerCase().includes(query),
    )
  )
}

export function componentDisplayName(kind: string): string {
  return kind.split('.').filter(Boolean).at(-1) ?? kind
}

export function componentFamily(kind: string): string {
  const parts = kind.split('.').filter(Boolean)
  return parts.length > 2 ? `${parts[0]}.${parts[1]}` : kind
}
