export type WorkspaceView = 'topology' | 'cases' | 'runs' | 'results'

export type WorkflowStageState =
  | 'complete'
  | 'ready'
  | 'attention'
  | 'locked'

export interface WorkflowInputs {
  componentCount: number
  connectionCount: number
  mediumCount: number
  hasCase: boolean
  unresolvedArtifactCount: number
  compiled: boolean
  variableCount: number
  equationCount: number
  runConfigurationCount: number
  activeJobCount: number
  succeededJobCount: number
}

export interface WorkflowStage {
  view: WorkspaceView
  number: number
  title: string
  description: string
  detail: string
  state: WorkflowStageState
}

function plural(count: number, word: string) {
  return `${count} ${word}${count === 1 ? '' : 's'}`
}

export function buildWorkflowStages(
  input: WorkflowInputs,
): WorkflowStage[] {
  const hasDraft = input.componentCount > 0
  const definitionInputsReady =
    input.hasCase && input.unresolvedArtifactCount === 0
  const hasRunConfiguration = input.runConfigurationCount > 0
  const hasSucceeded = input.succeededJobCount > 0

  const draftDetail = hasDraft
    ? [
        plural(input.componentCount, 'component'),
        plural(input.connectionCount, 'connection'),
        plural(input.mediumCount, 'medium'),
      ].join(' · ')
    : 'Start with a registered component template.'

  let definitionDetail = 'Create an operating case and define boundaries.'
  if (input.hasCase && input.unresolvedArtifactCount > 0) {
    definitionDetail = `${plural(
      input.unresolvedArtifactCount,
      'artifact binding',
    )} unresolved.`
  } else if (definitionInputsReady && !input.compiled) {
    definitionDetail = 'Compile the exact topology, case, and artifacts.'
  } else if (input.compiled) {
    definitionDetail = `${plural(input.variableCount, 'variable')} · ${plural(
      input.equationCount,
      'equation',
    )}`
  }

  let calculateDetail = 'Complete server validation first.'
  if (input.compiled && !hasRunConfiguration) {
    calculateDetail = 'Create an immutable run configuration.'
  } else if (input.activeJobCount > 0) {
    calculateDetail = `${plural(input.activeJobCount, 'calculation')} active.`
  } else if (hasSucceeded) {
    calculateDetail = 'A successful calculation is available.'
  } else if (hasRunConfiguration) {
    calculateDetail = `${plural(
      input.runConfigurationCount,
      'run configuration',
    )} ready.`
  }

  return [
    {
      view: 'topology',
      number: 1,
      title: 'Draft system',
      description: 'Components, connections, and media',
      detail: draftDetail,
      state: hasDraft ? 'complete' : 'attention',
    },
    {
      view: 'cases',
      number: 2,
      title: 'Define & validate',
      description: 'Parameters, boundaries, and artifacts',
      detail: definitionDetail,
      state: input.compiled
        ? 'complete'
        : hasDraft
          ? 'attention'
          : 'locked',
    },
    {
      view: 'runs',
      number: 3,
      title: 'Calculate',
      description: 'Configuration and execution',
      detail: calculateDetail,
      state: hasSucceeded
        ? 'complete'
        : input.compiled
          ? 'ready'
          : 'locked',
    },
    {
      view: 'results',
      number: 4,
      title: 'Analyze',
      description: 'Balances, trends, and exports',
      detail: hasSucceeded
        ? `${plural(input.succeededJobCount, 'successful result')} ready.`
        : 'Complete a calculation to unlock results.',
      state: hasSucceeded ? 'ready' : 'locked',
    },
  ]
}
