import type { ExecutionPreparationStage } from './executionPreparation'

interface ExecutionPreparationStripProps {
  stages: ExecutionPreparationStage[]
}

const stateLabel: Record<ExecutionPreparationStage['state'], string> = {
  complete: 'Complete',
  ready: 'Ready',
  attention: 'Needs review',
  locked: 'Locked',
}

export function ExecutionPreparationStrip({
  stages,
}: ExecutionPreparationStripProps) {
  return (
    <nav className="study-preparation-strip" aria-label="Execution preparation">
      {stages.map((stage) => (
        <a key={stage.id} className={stage.state} href={`#run-${stage.id}`}>
          <span>{stage.number}</span>
          <div>
            <strong>{stage.title}</strong>
            <small>{stage.detail}</small>
          </div>
          <em>{stateLabel[stage.state]}</em>
        </a>
      ))}
    </nav>
  )
}
