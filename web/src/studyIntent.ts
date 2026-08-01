export const studyModes = [
  {
    id: 'steady_state_design',
    title: 'Steady design / thermal balance',
    description:
      'Constrain a design or measured operating point and solve the remaining steady system state and balances.',
    execution: 'steady',
  },
  {
    id: 'steady_state_off_design',
    title: 'Steady off-design prediction',
    description:
      'Use physical component models and performance data to predict a steady operating point away from design.',
    execution: 'steady',
  },
  {
    id: 'dynamic_initialization',
    title: 'Transient initialization',
    description:
      'Establish a consistent differential-algebraic initial state before time integration.',
    execution: 'transient',
  },
  {
    id: 'dynamic_transient',
    title: 'Transient simulation',
    description:
      'Integrate the system state through time from defined initial conditions and controls.',
    execution: 'transient',
  },
] as const

export type StudyModeId = (typeof studyModes)[number]['id']

export function studyMode(mode: string) {
  return studyModes.find((item) => item.id === mode)
}
