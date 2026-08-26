import { describe, expect, it } from 'vitest'
import { buildValidationCampaign } from './validationCampaignAuthoring'

describe('validation campaign authoring', () => {
  it('builds a canonical declaration over exact Study revisions', () => {
    expect(buildValidationCampaign({
      artifactId: ' campaign-a ',
      name: ' Full-load checks ',
      objective: ' Compare against measured evidence ',
      studyRevisionIds: ['study-r2', 'study-r5'],
      limitationsText: 'Single ambient condition\nNo degradation model\n',
    })).toEqual({
      schema_version: 'thermox.validation_campaign/v1',
      id: 'campaign-a',
      name: 'Full-load checks',
      objective: 'Compare against measured evidence',
      study_revision_ids: ['study-r2', 'study-r5'],
      limitations: ['Single ambient condition', 'No degradation model'],
    })
  })

  it('requires at least one exact Study revision', () => {
    expect(() => buildValidationCampaign({
      artifactId: 'campaign-a',
      name: 'Campaign A',
      objective: 'Check the model',
      studyRevisionIds: [],
      limitationsText: '',
    })).toThrow('Select between 1 and 100')
  })

  it('rejects duplicate limitations', () => {
    expect(() => buildValidationCampaign({
      artifactId: 'campaign-a',
      name: 'Campaign A',
      objective: 'Check the model',
      studyRevisionIds: ['study-r1'],
      limitationsText: 'One condition\nOne condition',
    })).toThrow('Limitations must be unique')
  })
})
