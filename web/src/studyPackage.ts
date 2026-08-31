import { reviewTopologyJson } from './topologyJson'
import type {
  CaseDocument,
  CaseRevision,
  CreateRunConfiguration,
  CreateStudyRevision,
  RunConfigurationRevision,
  ModelRevision,
  ProjectModelValidation,
  StudyRevision,
  TopologyDocument,
} from './types'

export interface StudyPackageArtifactDependency {
  artifact_revision_id: string
  checksum: string
  artifact_id: string
  artifact_type: string
  artifact_schema_version: string
}

export type PackagedStudy = Omit<
  CreateStudyRevision,
  | 'schema_version'
  | 'parent_study_revision_id'
  | 'model_revision_id'
  | 'case_revision_id'
>

export type PackagedRunConfiguration = Omit<
  CreateRunConfiguration,
  | 'schema_version'
  | 'parent_run_configuration_revision_id'
  | 'study_revision_id'
>

export interface StudyPackageDocument {
  schema_version: 'thermox.study_package/v1'
  package_id: string
  topology: TopologyDocument
  case: CaseDocument
  artifact_dependencies: StudyPackageArtifactDependency[]
  study: PackagedStudy
  run_configuration?: PackagedRunConfiguration
}

export interface StudyPackageImportResult {
  schema_version: 'thermox.study_package_import/v1'
  package_id: string
  model_revision: ModelRevision
  case_revision: CaseRevision
  validation: ProjectModelValidation
  study_revision: StudyRevision
  run_configuration_revision: RunConfigurationRevision | null
}

export interface StudyPackageReview {
  document?: StudyPackageDocument
  issues: string[]
  summary?: {
    packageId: string
    modelId: string
    caseId: string
    studyId: string
    artifactCount: number
    hasRunConfiguration: boolean
  }
}

function record(value: unknown): value is Record<string, unknown> {
  return typeof value === 'object' && value !== null && !Array.isArray(value)
}

function text(value: unknown): value is string {
  return typeof value === 'string' && value.trim().length > 0
}

function array(value: unknown): value is unknown[] {
  return Array.isArray(value)
}

export function reviewStudyPackageJson(source: string): StudyPackageReview {
  let parsed: unknown
  try {
    parsed = JSON.parse(source)
  } catch (reason) {
    return { issues: [reason instanceof Error ? reason.message : 'Invalid JSON.'] }
  }
  const issues: string[] = []
  if (!record(parsed)) return { issues: ['Study package must be an object.'] }
  if (parsed.schema_version !== 'thermox.study_package/v1') {
    issues.push('schema_version must be thermox.study_package/v1.')
  }
  if (!text(parsed.package_id)) issues.push('package_id must be a non-empty string.')

  if (!record(parsed.topology)) {
    issues.push('topology must be a thermox.topology/v1 object.')
  } else {
    issues.push(...reviewTopologyJson(JSON.stringify(parsed.topology)).issues.map(
      (issue) => `topology: ${issue}`,
    ))
  }
  if (!record(parsed.case) || parsed.case.schema_version !== 'thermox.case/v1' ||
      !record(parsed.case.case)) {
    issues.push('case must be a thermox.case/v1 object.')
  } else {
    if (!text(parsed.case.case.id)) issues.push('case.case.id must be a non-empty string.')
    if (!text(parsed.case.case.mode)) issues.push('case.case.mode must be a non-empty string.')
  }

  const dependencies = parsed.artifact_dependencies
  const dependencyIds = new Set<string>()
  if (!array(dependencies)) {
    issues.push('artifact_dependencies must be an array.')
  } else {
    dependencies.forEach((dependency, index) => {
      if (!record(dependency)) {
        issues.push(`artifact_dependencies[${index}] must be an object.`)
        return
      }
      for (const field of [
        'artifact_revision_id', 'checksum', 'artifact_id',
        'artifact_type', 'artifact_schema_version',
      ]) {
        if (!text(dependency[field])) {
          issues.push(`artifact_dependencies[${index}].${field} must be a non-empty string.`)
        }
      }
      if (text(dependency.artifact_revision_id)) {
        if (dependencyIds.has(dependency.artifact_revision_id)) {
          issues.push(`Duplicate artifact dependency "${dependency.artifact_revision_id}".`)
        }
        dependencyIds.add(dependency.artifact_revision_id)
      }
    })
  }

  if (!record(parsed.study)) {
    issues.push('study must be an object.')
  } else {
    if (!text(parsed.study.study_id)) issues.push('study.study_id must be a non-empty string.')
    if (!text(parsed.study.intent)) issues.push('study.intent must be a non-empty string.')
    if (!array(parsed.study.artifact_revision_ids)) {
      issues.push('study.artifact_revision_ids must be an array.')
    } else {
      const selectedIds = new Set<string>()
      parsed.study.artifact_revision_ids.forEach((revisionId, index) => {
        if (!text(revisionId)) {
          issues.push(`study.artifact_revision_ids[${index}] must be a non-empty string.`)
        } else if (!dependencyIds.has(revisionId)) {
          issues.push(`Study artifact revision "${revisionId}" is not checksum-pinned in artifact_dependencies.`)
        } else {
          selectedIds.add(revisionId)
        }
      })
      for (const revisionId of dependencyIds) {
        if (!selectedIds.has(revisionId)) {
          issues.push(`Artifact dependency "${revisionId}" is not selected by the Study.`)
        }
      }
    }
    for (const field of [
      'artifact_qualification_requirements', 'artifact_operating_envelopes',
      'result_projections', 'acceptance_criteria', 'trajectory_validation_bindings',
    ]) {
      if (!array(parsed.study[field])) issues.push(`study.${field} must be an array.`)
    }
  }

  if (parsed.run_configuration !== undefined) {
    if (!record(parsed.run_configuration)) {
      issues.push('run_configuration must be an object when declared.')
    } else {
      if (!text(parsed.run_configuration.run_configuration_id)) {
        issues.push('run_configuration.run_configuration_id must be a non-empty string.')
      }
      if (!record(parsed.run_configuration.steady_solver)) {
        issues.push('run_configuration.steady_solver must be an object.')
      }
      if (!record(parsed.run_configuration.transient_solver)) {
        issues.push('run_configuration.transient_solver must be an object.')
      }
    }
  }

  const topology = record(parsed.topology) && record(parsed.topology.model)
    ? parsed.topology.model : undefined
  const caseDefinition = record(parsed.case) && record(parsed.case.case)
    ? parsed.case.case : undefined
  const study = record(parsed.study) ? parsed.study : undefined
  const summary = {
    packageId: text(parsed.package_id) ? parsed.package_id : '',
    modelId: topology && text(topology.id) ? topology.id : '',
    caseId: caseDefinition && text(caseDefinition.id) ? caseDefinition.id : '',
    studyId: study && text(study.study_id) ? study.study_id : '',
    artifactCount: array(dependencies) ? dependencies.length : 0,
    hasRunConfiguration: record(parsed.run_configuration),
  }
  return {
    ...(issues.length ? {} : { document: parsed as unknown as StudyPackageDocument }),
    issues,
    summary,
  }
}

export function studyPackageText(document: StudyPackageDocument): string {
  return `${JSON.stringify(document, null, 2)}\n`
}

export function packageStudyRevision(study: StudyRevision): PackagedStudy {
  return {
    study_id: study.study_id,
    intent: study.intent,
    artifact_revision_ids: [...study.artifact_revision_ids],
    artifact_qualification_requirements: study.artifact_qualification_requirements,
    artifact_operating_envelopes: study.artifact_operating_envelopes,
    result_projections: study.result_projections,
    acceptance_criteria: study.acceptance_criteria,
    trajectory_validation_bindings: study.trajectory_validation_bindings,
  }
}

export function packageRunConfiguration(
  revision: RunConfigurationRevision,
): PackagedRunConfiguration {
  return {
    run_configuration_id: revision.run_configuration_id,
    steady_solver: revision.steady_solver,
    transient_solver: revision.transient_solver,
  }
}
