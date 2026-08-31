import type { StudyPreparationStage } from './studyPreparation'

interface StudyPreparationStripProps {
  stages: StudyPreparationStage[]
}

const stateLabel: Record<StudyPreparationStage['state'], string> = {
  complete: 'Complete',
  ready: 'Ready',
  attention: 'Needs review',
  locked: 'Locked',
}

export function StudyPreparationStrip({
  stages,
}: StudyPreparationStripProps) {
  return (
    <nav className="study-preparation-strip" aria-label="Study preparation">
      {stages.map((stage) => (
        <a
          key={stage.id}
          className={stage.state}
          href={`#study-${stage.id}`}
        >
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
