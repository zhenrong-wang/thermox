import { describe, expect, it } from 'vitest'
import { jobLifecycle } from './jobLifecycle'

describe('jobLifecycle', () => {
  it('does not invent progress for a running worker', () => {
    expect(jobLifecycle('running').map((stage) => stage.status)).toEqual([
      'complete',
      'current',
      'pending',
    ])
  })

  it('represents failed and successful terminal outcomes', () => {
    expect(jobLifecycle('failed')[2]).toMatchObject({
      label: 'failed',
      status: 'error',
    })
    expect(jobLifecycle('succeeded')[2]).toMatchObject({
      label: 'succeeded',
      status: 'complete',
    })
  })

  it('does not claim worker execution for a job cancelled in the queue', () => {
    expect(jobLifecycle('cancelled', false)[1].status).toBe('pending')
    expect(jobLifecycle('cancelled', true)[1].status).toBe('complete')
  })
})
