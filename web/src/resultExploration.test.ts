import { describe, expect, it } from 'vitest'
import {
  filterResultRows,
  flattenGraphResult,
  resultRowsCsv,
  simulationResultCsv,
  transientSeries,
  transientSeriesOptions,
} from './resultExploration'
import type {
  CatalogUnitDimension,
  GraphResult,
  SteadySimulationResult,
  TransientSimulationResult,
} from './types'

const unitDimensions: CatalogUnitDimension[] = [
  {
    dimension: 'temperature',
    canonical_unit: 'K',
    si_display: {
      symbol: 'K',
      scale_from_si: 1,
      offset_from_si: 0,
    },
    engineering_display: {
      symbol: '°C',
      scale_from_si: 1,
      offset_from_si: -273.15,
    },
    accepted_units: [],
  },
]

function graph(temperature: number): GraphResult {
  return {
    system_balances: [
      { name: 'energy_residual', dimension: 'power', value_si: 0.25 },
    ],
    kpis: [{ name: 'efficiency', dimension: 'dimensionless', value_si: 0.4 }],
    components: [
      {
        component_id: 'compressor,1',
        kind: 'compressor.fluid',
        metrics: [],
        internal_values: [],
        ports: [
          {
            port_name: 'outlet',
            domain: 'fluid',
            medium_id: 'air',
            phase: 'gas',
            primary_values: [],
            derived_values: [
              {
                name: 'temperature',
                dimension: 'temperature',
                value_si: temperature,
                derivative_si_s: 2,
              },
            ],
          },
        ],
      },
    ],
  }
}

describe('result exploration', () => {
  it('flattens and filters every graph-native result scope', () => {
    const rows = flattenGraphResult(graph(400))
    expect(rows).toHaveLength(3)
    expect(filterResultRows(rows, 'AIR', 'all')).toHaveLength(1)
    expect(filterResultRows(rows, '', 'system_balance')).toHaveLength(1)
    expect(filterResultRows(rows, 'temperature', 'port_derived')[0]).toMatchObject({
      componentId: 'compressor,1',
      portName: 'outlet',
      valueSi: 400,
    })
  })

  it('exports canonical SI rows with safe CSV quoting', () => {
    const csv = resultRowsCsv(
      flattenGraphResult(graph(400)),
      3,
      1.5,
      unitDimensions,
    )
    expect(csv).toContain('sample_index,time_s,scope')
    expect(csv).toContain('3,1.5,port_derived,"compressor,1"')
    expect(csv).toContain(',temperature,temperature,K,400,2')
  })

  it('exports every transient sample and builds a stable signal series', () => {
    const result = {
      schema_version: 'thermox.result/v5',
      status: 'succeeded',
      trajectory: [
        { time: 0, graph: graph(300) },
        { time: 2, graph: graph(450) },
      ],
      events: [],
    } as unknown as TransientSimulationResult
    const option = transientSeriesOptions(result).find((value) =>
      value.label.includes('temperature'),
    )
    expect(option).toBeDefined()
    expect(transientSeries(result, option!.key)).toEqual([
      { time: 0, valueSi: 300 },
      { time: 2, valueSi: 450 },
    ])
    expect(
      simulationResultCsv(result, unitDimensions).split('\n'),
    ).toHaveLength(7)
  })

  it('exports a steady graph as sample zero without a time value', () => {
    const result = {
      schema_version: 'thermox.result/v5',
      status: 'succeeded',
      graph: graph(410),
    } as unknown as SteadySimulationResult
    expect(simulationResultCsv(result)).toContain(
      '0,,port_derived,"compressor,1"',
    )
  })
})
