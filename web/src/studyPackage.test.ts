import { describe, expect, it } from 'vitest'
import { reviewStudyPackageJson, studyPackageText, type StudyPackageDocument } from './studyPackage'

const document: StudyPackageDocument = {
  schema_version: 'thermox.study_package/v1',
  package_id: 'rankine-design',
  topology: {
    schema_version: 'thermox.topology/v1',
    model: {
      id: 'rankine', name: 'Rankine', revision: '1',
      media: [], components: [], connections: [],
    },
  },
  case: {
    schema_version: 'thermox.case/v1',
    case: { id: 'design', mode: 'steady_state_design' },
  },
  artifact_dependencies: [{
    artifact_revision_id: 'map-r1',
    checksum: 'sha256:map',
    artifact_id: 'pump-map',
    artifact_type: 'thermox.performance_map',
    artifact_schema_version: 'thermox.performance_map/v1',
  }],
  study: {
    study_id: 'design',
    intent: 'steady_state_design',
    artifact_revision_ids: ['map-r1'],
    artifact_qualification_requirements: [],
    artifact_operating_envelopes: [],
    result_projections: [],
    acceptance_criteria: [],
    trajectory_validation_bindings: [],
  },
}

describe('Study package declaration', () => {
  it('round-trips a complete revision-pinned declaration', () => {
    const review = reviewStudyPackageJson(studyPackageText(document))
    expect(review.issues).toEqual([])
    expect(review.document).toEqual(document)
    expect(review.summary).toMatchObject({
      modelId: 'rankine', caseId: 'design', studyId: 'design', artifactCount: 1,
    })
  })

  it('rejects unpinned Study artifact dependencies', () => {
    const invalid = structuredClone(document)
    invalid.study.artifact_revision_ids.push('correlation-r2')
    const review = reviewStudyPackageJson(JSON.stringify(invalid))
    expect(review.document).toBeUndefined()
    expect(review.issues).toContain(
      'Study artifact revision "correlation-r2" is not checksum-pinned in artifact_dependencies.',
    )
  })

  it('includes topology declaration diagnostics', () => {
    const invalid = structuredClone(document) as unknown as Record<string, unknown>
    ;(invalid.topology as { schema_version: string }).schema_version = 'invalid'
    expect(reviewStudyPackageJson(JSON.stringify(invalid)).issues).toContain(
      'topology: schema_version must be thermox.topology/v1.',
    )
  })
})
