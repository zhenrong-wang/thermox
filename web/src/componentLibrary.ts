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
    component.template_kind.toLowerCase().includes(query) ||
    component.display_name.toLowerCase().includes(query) ||
    component.category.toLowerCase().includes(query) ||
    component.model_name.toLowerCase().includes(query) ||
    component.system_boundary_role.toLowerCase().includes(query) ||
    component.default_mode.toLowerCase().includes(query) ||
    component.supported_modes.some((mode) =>
      mode.toLowerCase().includes(query),
    ) ||
    component.ports.some(
      (port) =>
        port.name.toLowerCase().includes(query) ||
        port.domain.toLowerCase().includes(query),
    )
  )
}
