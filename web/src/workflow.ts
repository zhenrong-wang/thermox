export type WorkspaceView =
  | 'topology'
  | 'definition'
  | 'studies'
  | 'runs'
  | 'results'

export type WorkflowStageState =
  | 'complete'
  | 'ready'
  | 'attention'
  | 'locked'

export interface WorkflowInputs {
  componentCount: number
  connectionCount: number
  mediumCount: number
  definitionIssueCount: number
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
    hasDraft && input.definitionIssueCount === 0
  const studyInputsReady =
    definitionInputsReady &&
    input.hasCase &&
    input.unresolvedArtifactCount === 0
  const hasRunConfiguration = input.runConfigurationCount > 0
  const hasSucceeded = input.succeededJobCount > 0

  const draftDetail = hasDraft
    ? [
        plural(input.componentCount, 'component'),
        plural(input.connectionCount, 'connection'),
        plural(input.mediumCount, 'medium'),
      ].join(' · ')
    : 'Start with a registered component template.'

  let definitionDetail = hasDraft
    ? `${plural(input.definitionIssueCount, 'physical input')} unresolved.`
    : 'Build the equipment topology first.'
  if (definitionInputsReady) {
    definitionDetail = 'All local component inputs are defined.'
  }

  let studyDetail = 'Create an operating case and define boundaries.'
  if (input.hasCase && input.unresolvedArtifactCount > 0) {
    studyDetail = `${plural(
      input.unresolvedArtifactCount,
      'artifact binding',
    )} unresolved.`
  } else if (studyInputsReady && !input.compiled) {
    studyDetail = 'Compile the exact topology, case, and artifacts.'
  } else if (input.compiled) {
    studyDetail = `${plural(input.variableCount, 'variable')} · ${plural(
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
      view: 'definition',
      number: 2,
      title: 'Define system',
      description: 'Physics, media, parameters, and data',
      detail: definitionDetail,
      state: definitionInputsReady
        ? 'complete'
        : hasDraft
          ? 'attention'
          : 'locked',
    },
    {
      view: 'studies',
      number: 3,
      title: 'Define study',
      description: 'Intent, boundaries, targets, and outputs',
      detail: studyDetail,
      state: input.compiled
        ? 'complete'
        : definitionInputsReady
          ? 'attention'
          : 'locked',
    },
    {
      view: 'runs',
      number: 4,
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
      number: 5,
      title: 'Analyze',
      description: 'Balances, trends, and exports',
      detail: hasSucceeded
        ? `${plural(input.succeededJobCount, 'successful result')} ready.`
        : 'Complete a calculation to unlock results.',
      state: hasSucceeded ? 'ready' : 'locked',
    },
  ]
}
