import { useMemo, useRef, useState, type ChangeEvent, type FormEvent } from 'react'
import {
  reviewStudyPackageJson,
  studyPackageText,
  type StudyPackageDocument,
} from './studyPackage'

interface StudyPackageWorkbenchProps {
  initial?: StudyPackageDocument
  publishing: boolean
  onCancel: () => void
  onImport: (document: StudyPackageDocument) => Promise<void>
}

function safeFilename(value: string): string {
  return value.replace(/[^A-Za-z0-9._-]+/g, '-').replace(/^-+|-+$/g, '') || 'study'
}

export function StudyPackageWorkbench({
  initial,
  publishing,
  onCancel,
  onImport,
}: StudyPackageWorkbenchProps) {
  const selectedText = initial ? studyPackageText(initial) : ''
  const [source, setSource] = useState(selectedText)
  const [status, setStatus] = useState('')
  const [error, setError] = useState('')
  const fileInput = useRef<HTMLInputElement>(null)
  const review = useMemo(() => reviewStudyPackageJson(source), [source])

  async function loadFile(event: ChangeEvent<HTMLInputElement>) {
    const file = event.target.files?.[0]
    if (!file) return
    setError('')
    setStatus('')
    try {
      setSource(await file.text())
      setStatus(`Loaded ${file.name}; review dependencies before importing.`)
    } catch (reason) {
      setError(reason instanceof Error ? reason.message : 'Could not read the selected file.')
    } finally {
      event.target.value = ''
    }
  }

  async function copyPackage() {
    if (!review.document) return
    try {
      await navigator.clipboard.writeText(studyPackageText(review.document))
      setStatus('Copied the reviewed Study package JSON.')
    } catch {
      setError('Clipboard access is unavailable. Select and copy the JSON manually.')
    }
  }

  function downloadPackage() {
    if (!review.document) return
    const blob = new Blob([studyPackageText(review.document)], { type: 'application/json' })
    const url = URL.createObjectURL(blob)
    const anchor = document.createElement('a')
    anchor.href = url
    anchor.download = `${safeFilename(review.document.package_id)}.thermox.study.json`
    anchor.click()
    URL.revokeObjectURL(url)
    setStatus('Downloaded the reviewed Study package JSON.')
  }

  async function submit(event: FormEvent) {
    event.preventDefault()
    if (!review.document) return
    setError('')
    setStatus('')
    try {
      await onImport(review.document)
      onCancel()
    } catch (reason) {
      setError(reason instanceof Error ? reason.message : 'Study package import was rejected.')
    }
  }

  return (
    <div className="dialog-backdrop" role="presentation">
      <form className="component-dialog topology-json-dialog" onSubmit={submit}>
        <header>
          <div>
            <span className="eyebrow">Declarative calculation workflow</span>
            <h2>Study package JSON</h2>
          </div>
          <button type="button" className="icon-button" onClick={onCancel}>×</button>
        </header>
        <p className="form-note">
          Imports topology, case, Study intent, and optional solver configuration.
          Referenced engineering artifacts must already exist at the pinned checksums.
        </p>
        <div className="topology-json-actions">
          <button type="button" className="secondary-button"
            onClick={() => fileInput.current?.click()}>Load JSON file</button>
          <input ref={fileInput} type="file" accept="application/json,.json"
            hidden onChange={(event) => void loadFile(event)} />
          <button type="button" className="secondary-button" disabled={!initial}
            onClick={() => { setSource(selectedText); setError(''); setStatus('Restored the selected Study package.') }}>
            Restore selected
          </button>
          <button type="button" className="secondary-button" disabled={!review.document}
            onClick={() => void copyPackage()}>Copy package</button>
          <button type="button" className="secondary-button" disabled={!review.document}
            onClick={downloadPackage}>Download package</button>
        </div>
        <textarea className="topology-json-editor" spellCheck={false}
          aria-label="Study package JSON document" value={source}
          placeholder="Paste or load thermox.study_package/v1 JSON"
          onChange={(event) => { setSource(event.target.value); setError(''); setStatus('') }} />
        <div className={`topology-json-review${review.issues.length ? ' is-invalid' : ' is-valid'}`}>
          <div>
            <strong>{review.issues.length ? 'Package review found issues' : 'Package shape is ready for dependency verification'}</strong>
            {review.summary && <span>
              {review.summary.modelId || '—'} · {review.summary.caseId || '—'} ·{' '}
              {review.summary.artifactCount} artifacts ·{' '}
              {review.summary.hasRunConfiguration ? 'solver included' : 'no solver'}
            </span>}
          </div>
          {review.issues.length > 0 && (
            <ul>{review.issues.slice(0, 12).map((issue) => <li key={issue}>{issue}</li>)}</ul>
          )}
          <small>
            Import verifies artifact identity and checksum first, then publishes immutable
            topology, case, Study, and run-configuration revisions through their normal APIs.
          </small>
        </div>
        {(error || status) && <div className={`form-error topology-json-message${error ? '' : ' is-status'}`}>
          {error || status}
        </div>}
        <footer>
          <button type="button" className="secondary-button" onClick={onCancel}>Cancel</button>
          <button type="submit" className="primary-button"
            disabled={publishing || !review.document}>
            {publishing ? 'Importing…' : 'Verify dependencies and import'}
          </button>
        </footer>
      </form>
    </div>
  )
}
