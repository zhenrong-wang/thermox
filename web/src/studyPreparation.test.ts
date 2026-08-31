import { describe, expect, it } from 'vitest'
import {
  buildStudyPreparationStages,
  resolveStudyArtifactSelections,
  selectedStudyArtifactRevisionIds,
  studyMatchesPreparationSelection,
} from './studyPreparation'
import type { ArtifactRevision } from './types'

describe('Study preparation stages', () => {
  it('guides an incomplete case without claiming compiler authority', () => {
    const stages = buildStudyPreparationStages({
      recognizedIntent: true,
      localDefinitionIssueCount: 2,
      boundaryValueCount: 0,
      requiredArtifactCount: 1,
      unresolvedArtifactCount: 1,
      exactRevisionCompiled: false,
      hasPublishedStudy: false,
    })

    expect(stages.map((stage) => stage.state)).toEqual([
      'complete',
      'attention',
      'attention',
      'locked',
    ])
    expect(stages[1].detail).toContain('physical definition issues')
  })

  it('keeps a zero-boundary case reviewable and lets the compiler decide', () => {
    const stages = buildStudyPreparationStages({
      recognizedIntent: true,
      localDefinitionIssueCount: 0,
      boundaryValueCount: 0,
      requiredArtifactCount: 0,
      unresolvedArtifactCount: 0,
      exactRevisionCompiled: false,
      hasPublishedStudy: false,
    })

    expect(stages[1].state).toBe('attention')
    expect(stages[1].detail).toContain('compiler decides sufficiency')
    expect(stages[2].state).toBe('ready')
  })

  it('unlocks publication only after exact-revision compilation', () => {
    const stages = buildStudyPreparationStages({
      recognizedIntent: true,
      localDefinitionIssueCount: 0,
      boundaryValueCount: 4,
      requiredArtifactCount: 2,
      unresolvedArtifactCount: 0,
      exactRevisionCompiled: true,
      hasPublishedStudy: false,
    })

    expect(stages.map((stage) => stage.state)).toEqual([
      'complete',
      'complete',
      'complete',
      'ready',
    ])
  })

  it('preserves an explicit available artifact selection over defaults', () => {
    const revisions = [
      {
        artifact_id: 'compressor-map',
        artifact_revision_id: 'map-r2',
        revision_number: 2,
      },
      {
        artifact_id: 'compressor-map',
        artifact_revision_id: 'map-r1',
        revision_number: 1,
      },
    ] as ArtifactRevision[]

    const selections = resolveStudyArtifactSelections(
      ['compressor-map'],
      revisions,
      { 'compressor-map': 'map-r2' },
      { 'compressor-map': 'map-r1' },
    )

    expect(selections).toEqual({ 'compressor-map': 'map-r1' })
    expect(
      selectedStudyArtifactRevisionIds(['compressor-map'], selections),
    ).toEqual(['map-r1'])
  })

  it('drops stale selections and resolves the newest available revision', () => {
    const revisions = [{
      artifact_id: 'valve-correlation',
      artifact_revision_id: 'correlation-r3',
      revision_number: 3,
    }] as ArtifactRevision[]

    expect(
      resolveStudyArtifactSelections(
        ['valve-correlation'],
        revisions,
        {},
        { 'valve-correlation': 'deleted-r1' },
      ),
    ).toEqual({ 'valve-correlation': 'correlation-r3' })
  })

  it('recognizes a Study with additional Study-owned evidence artifacts', () => {
    const study = {
      case_revision_id: 'case-r4',
      artifact_revision_ids: [
        'compressor-map-r2',
        'transient-evidence-r1',
      ],
    } as import('./types').StudyRevision

    expect(
      studyMatchesPreparationSelection(
        study,
        'case-r4',
        ['compressor-map-r2'],
      ),
    ).toBe(true)
    expect(
      studyMatchesPreparationSelection(
        study,
        'case-r4',
        ['compressor-map-r3'],
      ),
    ).toBe(false)
  })
})
