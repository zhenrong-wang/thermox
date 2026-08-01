import { describe, expect, it } from 'vitest'
import { studyMode, studyModes } from './studyIntent'

describe('study intent presentation', () => {
  it('maps every durable case mode to an execution family', () => {
    expect(studyModes).toHaveLength(4)
    expect(studyModes.every((mode) => mode.execution)).toBe(true)
    expect(studyMode('steady_state_off_design')).toMatchObject({
      execution: 'steady',
      title: 'Steady off-design prediction',
    })
  })
})
