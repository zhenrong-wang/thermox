import type { WorkflowStage, WorkspaceView } from './workflow'

interface WorkflowNavigatorProps {
  currentView: WorkspaceView
  stages: WorkflowStage[]
  calculatable: boolean
  onSelect: (view: WorkspaceView) => void
}

const stateLabel: Record<WorkflowStage['state'], string> = {
  complete: 'Complete',
  ready: 'Ready',
  attention: 'Needs input',
  locked: 'Not ready',
}

export function WorkflowNavigator({
  currentView,
  stages,
  calculatable,
  onSelect,
}: WorkflowNavigatorProps) {
  return (
    <>
      <nav className="workflow-nav" aria-label="Engineering workflow">
        {stages.map((stage) => (
          <button
            type="button"
            key={stage.view}
            className={`workflow-step ${stage.state}${
              currentView === stage.view ? ' active' : ''
            }`}
            aria-current={currentView === stage.view ? 'step' : undefined}
            onClick={() => onSelect(stage.view)}
          >
            <span className="workflow-step-number">{stage.number}</span>
            <span className="workflow-step-copy">
              <strong>{stage.title}</strong>
              <small>{stage.description}</small>
              <em>{stage.detail}</em>
            </span>
            <span className="workflow-step-state">
              {stateLabel[stage.state]}
            </span>
          </button>
        ))}
      </nav>
      <section
        className={`calculation-readiness${calculatable ? ' ready' : ''}`}
        aria-live="polite"
      >
        <span>Calculation readiness</span>
        <strong>{calculatable ? 'Calculatable' : 'Definition incomplete'}</strong>
        <p>
          {calculatable
            ? 'The service compiled this exact topology, case, and artifact set.'
            : 'Only server compilation can establish that the selected system is calculatable.'}
        </p>
        {!calculatable && (
          <button type="button" onClick={() => onSelect('studies')}>
            Review readiness
          </button>
        )}
      </section>
    </>
  )
}
