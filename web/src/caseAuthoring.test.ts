import { describe, expect, it } from 'vitest'
import { buildMetadataEdits, buildScalarEdit } from './caseAuthoring'

describe('case authoring operations', () => {
  it('preserves engineering units for service-authoritative normalization', () => {
    expect(buildScalarEdit('fixed_value', ' compressor.inlet.p ', '2', 'bar'))
      .toEqual({
        action: 'upsert',
        field: 'fixed_value',
        key: 'compressor.inlet.p',
        value: { value: 2, unit: 'bar' },
      })
  })

  it('emits only changed metadata and removes a cleared label', () => {
    expect(
      buildMetadataEdits(
        {
          id: 'design',
          label: 'Hot day',
          mode: 'steady_state_design',
        },
        '',
        'steady_state_off_design',
      ),
    ).toEqual([
      { action: 'remove', field: 'label' },
      {
        action: 'upsert',
        field: 'mode',
        value: 'steady_state_off_design',
      },
    ])
  })
})
