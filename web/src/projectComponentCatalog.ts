import type {
  Catalog,
  ProjectComponentCatalogEntry,
  TopologyDocument,
} from './types'

export function mergeProjectComponentCatalog(
  catalog: Catalog,
  projectComponents: ProjectComponentCatalogEntry[],
): Catalog {
  const latest = new Map<
    string,
    ProjectComponentCatalogEntry
  >()
  for (const entry of projectComponents) {
    const current = latest.get(entry.component.kind)
    if (
      !current ||
      current.source.created_at_epoch_ms <
        entry.source.created_at_epoch_ms ||
      (current.source.created_at_epoch_ms ===
        entry.source.created_at_epoch_ms &&
        current.source.revision_number <
          entry.source.revision_number)
    ) {
      latest.set(entry.component.kind, entry)
    }
  }
  const custom = [...latest.values()].map((entry) => ({
    ...entry.component,
    source_artifact_id: entry.source.artifact_id,
    source_artifact_revision_id:
      entry.source.artifact_revision_id,
  }))
  return {
    ...catalog,
    fingerprint:
      projectComponents[0]?.catalog_fingerprint ??
      catalog.fingerprint,
    components: [...catalog.components, ...custom].sort(
      (left, right) => left.kind.localeCompare(right.kind),
    ),
  }
}

export function requiredProjectComponentSources(
  topology: TopologyDocument | undefined,
  projectComponents: ProjectComponentCatalogEntry[],
): ProjectComponentCatalogEntry[] {
  const selected = new Map<string, ProjectComponentCatalogEntry>()
  for (const instance of topology?.model.components ?? []) {
    const source = projectComponents.find(
      (entry) =>
        entry.component.kind === instance.kind &&
        (!instance.version ||
          entry.component.version === instance.version),
    )
    if (source) {
      selected.set(source.source.artifact_id, source)
    }
  }
  return [...selected.values()].sort((left, right) =>
    left.source.artifact_id.localeCompare(
      right.source.artifact_id,
    ),
  )
}
