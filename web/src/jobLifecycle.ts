import type { SimulationJobState } from './types'

export type LifecycleStageStatus =
  | 'pending'
  | 'current'
  | 'complete'
  | 'error'

export interface LifecycleStage {
  id: 'queued' | 'running' | 'terminal'
  label: string
  status: LifecycleStageStatus
}

export function jobLifecycle(
  state: SimulationJobState,
  workerClaimed = false,
): LifecycleStage[] {
  const terminal =
    state === 'succeeded' || state === 'failed' || state === 'cancelled'
  return [
    {
      id: 'queued',
      label: 'Queued',
      status: state === 'queued' ? 'current' : 'complete',
    },
    {
      id: 'running',
      label: 'Worker execution',
      status:
        state === 'queued'
          ? 'pending'
          : state === 'cancelled' && !workerClaimed
            ? 'pending'
            : state === 'running'
              ? 'current'
              : 'complete',
    },
    {
      id: 'terminal',
      label: terminal ? state : 'Result',
      status:
        state === 'failed' || state === 'cancelled'
          ? 'error'
          : state === 'succeeded'
            ? 'complete'
            : 'pending',
    },
  ]
}
