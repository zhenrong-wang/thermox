import type { PerformanceMapArtifactDefinition } from './types'

const maximumImportCharacters = 20 * 1024 * 1024

export interface DelimitedTable {
  headers: string[]
  rows: string[][]
  delimiter: ',' | '\t' | ';'
}

export interface PerformanceMapColumnMapping {
  primary: string
  family: string
  outputs: string[]
}

function delimiterCount(line: string, delimiter: string): number {
  let count = 0
  let quoted = false
  for (let index = 0; index < line.length; index += 1) {
    if (line[index] === '"') {
      if (quoted && line[index + 1] === '"') index += 1
      else quoted = !quoted
    } else if (!quoted && line[index] === delimiter) {
      count += 1
    }
  }
  return count
}

function rows(text: string, delimiter: string): string[][] {
  const result: string[][] = []
  let row: string[] = []
  let field = ''
  let quoted = false
  for (let index = 0; index < text.length; index += 1) {
    const character = text[index]
    if (quoted) {
      if (character === '"') {
        if (text[index + 1] === '"') {
          field += '"'
          index += 1
        } else {
          quoted = false
        }
      } else {
        field += character
      }
    } else if (character === '"' && !field.trim()) {
      quoted = true
    } else if (character === delimiter) {
      row.push(field.trim())
      field = ''
    } else if (character === '\n') {
      row.push(field.trim())
      result.push(row)
      row = []
      field = ''
    } else if (character !== '\r') {
      field += character
    }
  }
  if (quoted) throw new Error('The imported table has an unterminated quoted value.')
  if (field || row.length) {
    row.push(field.trim())
    result.push(row)
  }
  return result.filter((item) => item.some(Boolean))
}

export function parseDelimitedTable(text: string): DelimitedTable {
  if (text.length > maximumImportCharacters) {
    throw new Error('The imported table exceeds the 20 MiB interactive authoring limit.')
  }
  const normalized = text.replace(/^\uFEFF/, '')
  const firstLine = normalized.split(/\r?\n/, 1)[0] ?? ''
  const candidates = [',', '\t', ';'] as const
  const delimiter = candidates.reduce((selected, candidate) =>
    delimiterCount(firstLine, candidate) > delimiterCount(firstLine, selected)
      ? candidate
      : selected,
  )
  if (!delimiterCount(firstLine, delimiter)) {
    throw new Error('The imported file needs a comma, tab, or semicolon-delimited header row.')
  }
  const parsed = rows(normalized, delimiter)
  if (parsed.length < 2) throw new Error('The imported table needs a header and at least one data row.')
  const headers = parsed[0]
  if (headers.some((header) => !header)) throw new Error('Imported column names must not be empty.')
  if (new Set(headers).size !== headers.length) throw new Error('Imported column names must be unique.')
  parsed.slice(1).forEach((row, index) => {
    if (row.length !== headers.length) {
      throw new Error(`Imported row ${index + 2} has ${row.length} columns; expected ${headers.length}.`)
    }
  })
  return { headers, rows: parsed.slice(1), delimiter }
}

export function mapCurvesFromTable(
  table: DelimitedTable,
  mapping: PerformanceMapColumnMapping,
): PerformanceMapArtifactDefinition['curves'] {
  if (!mapping.outputs.length) {
    throw new Error('Declare and map at least one performance-map output.')
  }
  const selected = [mapping.family, mapping.primary, ...mapping.outputs]
  if (selected.some((column) => !table.headers.includes(column))) {
    throw new Error('Map every axis and output to an imported column.')
  }
  if (new Set(selected).size !== selected.length) {
    throw new Error('Each axis and output must use a different imported column.')
  }
  const indexes = selected.map((column) => table.headers.indexOf(column))
  const grouped = new Map<number, Map<number, number[]>>()
  table.rows.forEach((row, rowIndex) => {
    const cells = indexes.map((index) => row[index])
    const values = cells.map(Number)
    if (
      cells.some((cell) => !cell) ||
      values.some((value) => !Number.isFinite(value))
    ) {
      throw new Error(`Imported row ${rowIndex + 2} contains a missing or non-finite mapped value.`)
    }
    const [family, primary, ...outputs] = values
    const samples = grouped.get(family) ?? new Map<number, number[]>()
    if (samples.has(primary)) {
      throw new Error(
        `Imported row ${rowIndex + 2} duplicates family ${family}, primary ${primary}.`,
      )
    }
    samples.set(primary, outputs)
    grouped.set(family, samples)
  })
  const curves = [...grouped.entries()]
    .sort(([left], [right]) => left - right)
    .map(([family_coordinate, samples]) => ({
      family_coordinate,
      samples: [...samples.entries()]
        .sort(([left], [right]) => left - right)
        .map(([coordinate, outputs]) => ({ coordinate, outputs })),
    }))
  if (curves.length < 2) {
    throw new Error('Imported map data needs at least two distinct family coordinates.')
  }
  const incomplete = curves.find((curve) => curve.samples.length < 2)
  if (incomplete) {
    throw new Error(
      `Imported family ${incomplete.family_coordinate} needs at least two distinct primary coordinates.`,
    )
  }
  return curves
}

export function suggestedColumn(
  headers: string[],
  variableName: string,
  fallback = '',
): string {
  const normalized = variableName.toLowerCase().replace(/[^a-z0-9]/g, '')
  return headers.find(
    (header) => header.toLowerCase().replace(/[^a-z0-9]/g, '') === normalized,
  ) ?? fallback
}
