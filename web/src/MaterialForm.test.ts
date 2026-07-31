import { describe, expect, it } from 'vitest'
import { parseSpecies } from './MaterialForm'

describe('parseSpecies', () => {
  it('accepts common separators and preserves first-seen order', () => {
    expect(parseSpecies('N2, O2\nCH4; O2 H2O')).toEqual([
      'N2',
      'O2',
      'CH4',
      'H2O',
    ])
  })
})
