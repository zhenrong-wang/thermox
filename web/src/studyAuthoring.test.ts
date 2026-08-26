import { describe, expect, it } from 'vitest'
import {
  parseValidationSeriesArtifact,
  studyArtifactRevisionIds,
} from './studyAuthoring'
import type { StudyTrajectoryValidationBinding } from './types'

const validationSeries = {
  schema_version: 'thermox.validation_series/v1',
  id: 'shaft-test',
  source: {
    reference: 'independent test',
    checksum_sha256: 'a'.repeat(64),
    evidence_basis: 'independent_reference',
    acquisition: 'measured',
    limitations: [],
  },
  time_unit: 's',
  signals: [{
    id: 'speed',
    dimension: 'angular_speed',
    unit: 'rpm',
    samples: [{ time: 0, value: 3000 }],
  }],
}

describe('Study evidence authoring', () => {
  it('accepts a matching validation-series declaration', () => {
    expect(parseValidationSeriesArtifact(
      JSON.stringify(validationSeries),
      'shaft-test',
    )).toEqual(validationSeries)
  })

  it('rejects a declaration whose immutable identity differs', () => {
    expect(() => parseValidationSeriesArtifact(
      JSON.stringify(validationSeries),
      'another-test',
    )).toThrow('Artifact ID must match')
  })

  it('pins evidence revisions in addition to physical artifacts', () => {
    const binding = {
      id: 'speed-validation',
      artifact_revision_id: 'evidence-r1',
    } as StudyTrajectoryValidationBinding
    expect(studyArtifactRevisionIds(
      ['map-r2', 'evidence-r1'],
      [binding],
    )).toEqual(['evidence-r1', 'map-r2'])
  })
})
