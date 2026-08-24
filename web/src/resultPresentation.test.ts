import { describe, expect, it } from 'vitest'
import {
  formatResultValue,
  projectedGraphNodeValues,
  resultGraph,
  resultSampleCount,
  resultSampleTime,
} from './resultPresentation'
import type {
  GraphResult,
  SimulationJob,
  TransientSimulationResult,
} from './types'

const emptyGraph: GraphResult = {
  components: [],
  system_balances: [],
  kpis: [],
}

describe('result presentation', () => {
  it('selects a bounded transient graph sample', () => {
    const result = {
      schema_version: 'thermox.result/v4',
      status: 'succeeded',
      trajectory: [
        { time: 0, graph: emptyGraph },
        { time: 2, graph: { ...emptyGraph, kpis: [{ name: 'x', dimension: 'none', value_si: 4 }] } },
      ],
      events: [],
    } as unknown as TransientSimulationResult

    expect(resultSampleCount(result)).toBe(2)
    expect(resultSampleTime(result, 99)).toBe(2)
    expect(resultGraph(result, 99)?.kpis[0]?.value_si).toBe(4)
  })

  it('maps projected summaries back to component and port identity', () => {
    const job = {
      request: {
        result_projections: [
          {
            id: 'outlet_temperature',
            scope: 'port_derived',
            component_id: 'compressor',
            port_name: 'outlet',
            value_name: 'T',
            dimension: 'temperature',
            aggregation: 'final',
          },
        ],
      },
      result_summary: {
        schema_version: 'thermox.result_summary/v3',
        mode: 'steady',
        values: [
          {
            id: 'outlet_temperature',
            dimension: 'temperature',
            value_si: 400,
            aggregation: 'final',
            sample_time: null,
            window: null,
          },
        ],
      },
    } as SimulationJob

    const graph: GraphResult = {
      ...emptyGraph,
      components: [
        {
          component_id: 'compressor',
          kind: 'compressor.fluid.isentropic_efficiency',
          metrics: [],
          internal_values: [],
          ports: [
            {
              port_name: 'outlet',
              domain: 'fluid',
              medium_id: 'air',
              phase: 'vapor',
              primary_values: [],
              derived_values: [
                {
                  name: 'T',
                  dimension: 'temperature',
                  value_si: 410,
                },
              ],
            },
          ],
        },
      ],
    }

    expect(projectedGraphNodeValues(job, graph)).toEqual({
      compressor: [
        {
          label: 'outlet.T',
          dimension: 'temperature',
          valueSi: 410,
        },
      ],
    })
    expect(formatResultValue(12.3456789)).toBe('12.345679')
  })
})
