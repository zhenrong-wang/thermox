import { describe, expect, it } from 'vitest'
import {
  mapCurvesFromTable,
  parseDelimitedTable,
  suggestedColumn,
} from './performanceMapImport'

describe('performance map tabular import', () => {
  it('detects CSV and handles quoted headers and values', () => {
    const table = parseDelimitedTable(
      '\uFEFF"speed","flow","pressure ratio","note"\r\n400,120,10,"OEM ""A"""\r\n250,70,8,baseline\r\n',
    )
    expect(table.delimiter).toBe(',')
    expect(table.headers).toEqual(['speed', 'flow', 'pressure ratio', 'note'])
    expect(table.rows[0]).toEqual(['400', '120', '10', 'OEM "A"'])
  })

  it('detects tab-delimited tables', () => {
    expect(parseDelimitedTable('speed\tflow\tefficiency\n1\t2\t0.8').delimiter).toBe('\t')
  })

  it('maps, groups, and orders non-rectangular family curves', () => {
    const table = parseDelimitedTable(
      'speed,flow,ratio,eff\n400,120,10,0.86\n250,120,8,0.84\n400,70,12,0.84\n250,70,10,0.82',
    )
    expect(mapCurvesFromTable(table, {
      family: 'speed',
      primary: 'flow',
      outputs: ['ratio', 'eff'],
    })).toEqual([
      {
        family_coordinate: 250,
        samples: [
          { coordinate: 70, outputs: [10, 0.82] },
          { coordinate: 120, outputs: [8, 0.84] },
        ],
      },
      {
        family_coordinate: 400,
        samples: [
          { coordinate: 70, outputs: [12, 0.84] },
          { coordinate: 120, outputs: [10, 0.86] },
        ],
      },
    ])
  })

  it('rejects reused columns and duplicate operating points', () => {
    const table = parseDelimitedTable('speed,flow,eff\n1,2,0.8\n1,2,0.9')
    expect(() => mapCurvesFromTable(table, {
      family: 'speed', primary: 'flow', outputs: ['flow'],
    })).toThrow('Each axis and output must use a different imported column.')
    expect(() => mapCurvesFromTable(table, {
      family: 'speed', primary: 'flow', outputs: ['eff'],
    })).toThrow('duplicates family 1, primary 2')
  })

  it('rejects tables that cannot form an interpolable surface', () => {
    const table = parseDelimitedTable('speed,flow,eff\n1,1,0.8\n1,2,0.81')
    expect(() => mapCurvesFromTable(table, {
      family: 'speed', primary: 'flow', outputs: ['eff'],
    })).toThrow('at least two distinct family coordinates')
  })

  it('does not coerce missing mapped cells to zero', () => {
    const table = parseDelimitedTable('speed,flow,eff\n1,1,0.8\n1,2,\n2,1,0.82\n2,2,0.83')
    expect(() => mapCurvesFromTable(table, {
      family: 'speed', primary: 'flow', outputs: ['eff'],
    })).toThrow('missing or non-finite mapped value')
  })

  it('suggests columns independent of spaces and punctuation', () => {
    expect(suggestedColumn(['Corrected Mass Flow', 'PR'], 'corrected_mass_flow')).toBe(
      'Corrected Mass Flow',
    )
  })
})
