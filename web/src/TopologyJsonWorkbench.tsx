import { useMemo, useRef, useState, type ChangeEvent, type FormEvent } from 'react'
import { reviewTopologyJson, topologyJsonText } from './topologyJson'
import type { ModelRevision, TopologyDocument } from './types'

interface TopologyJsonWorkbenchProps {
  topology?: TopologyDocument
  revision?: ModelRevision
  publishing: boolean
  onCancel: () => void
  onPublish: (document: TopologyDocument) => Promise<void>
}

function emptyDocument(): TopologyDocument {
  return {
    schema_version: 'thermox.topology/v1',
    model: {
      id: 'thermal_system',
      name: 'Thermal system',
      revision: '1',
      media: [],
      materials: [],
      components: [],
      assemblies: [],
      connections: [],
    },
  }
}

function safeFilename(value: string): string {
  return value.replace(/[^A-Za-z0-9._-]+/g, '-').replace(/^-+|-+$/g, '') || 'topology'
}

export function TopologyJsonWorkbench({
  topology,
  revision,
  publishing,
  onCancel,
  onPublish,
}: TopologyJsonWorkbenchProps) {
  const selectedText = topologyJsonText(topology ?? emptyDocument())
  const [source, setSource] = useState(selectedText)
  const [status, setStatus] = useState('')
  const [error, setError] = useState('')
  const fileInput = useRef<HTMLInputElement>(null)
  const review = useMemo(() => reviewTopologyJson(source), [source])
  const currentModelId = topology?.model.id
  const changesModelIdentity = Boolean(
    currentModelId && review.summary?.modelId &&
    currentModelId !== review.summary.modelId,
  )

  async function loadFile(event: ChangeEvent<HTMLInputElement>) {
    const file = event.target.files?.[0]
    if (!file) return
    setError('')
    setStatus('')
    try {
      setSource(await file.text())
      setStatus(`Loaded ${file.name}; review before publishing.`)
    } catch (reason) {
      setError(reason instanceof Error ? reason.message : 'Could not read the selected file.')
    } finally {
      event.target.value = ''
    }
  }

  async function copySelected() {
    setError('')
    try {
      await navigator.clipboard.writeText(selectedText)
      setStatus('Copied the selected revision JSON.')
    } catch {
      setError('Clipboard access is unavailable. Select and copy the JSON text manually.')
    }
  }

  function downloadSelected() {
    const blob = new Blob([selectedText], { type: 'application/json' })
    const url = URL.createObjectURL(blob)
    const anchor = document.createElement('a')
    anchor.href = url
    anchor.download = `${safeFilename(topology?.model.id ?? 'topology')}.thermox.topology.json`
    anchor.click()
    URL.revokeObjectURL(url)
    setStatus('Downloaded the selected revision JSON.')
  }

  async function publish(event: FormEvent) {
    event.preventDefault()
    if (!review.document) return
    setError('')
    setStatus('')
    try {
      await onPublish(review.document)
      onCancel()
    } catch (reason) {
      setError(reason instanceof Error ? reason.message : 'Topology publication was rejected.')
    }
  }

  return (
    <div className="dialog-backdrop" role="presentation">
      <form className="component-dialog topology-json-dialog" onSubmit={publish}>
        <header>
          <div>
            <span className="eyebrow">Declaration workbench</span>
            <h2>Topology JSON</h2>
          </div>
          <button type="button" className="icon-button" onClick={onCancel}>×</button>
        </header>
        <div className="topology-json-actions">
          <button type="button" className="secondary-button"
            onClick={() => fileInput.current?.click()}>Load JSON file</button>
          <input ref={fileInput} type="file" accept="application/json,.json"
            hidden onChange={(event) => void loadFile(event)} />
          <button type="button" className="secondary-button"
            onClick={() => { setSource(selectedText); setError(''); setStatus('Restored the selected revision.') }}>
            Restore selected
          </button>
          <button type="button" className="secondary-button" disabled={!topology}
            onClick={() => void copySelected()}>Copy selected JSON</button>
          <button type="button" className="secondary-button" disabled={!topology}
            onClick={downloadSelected}>Download selected JSON</button>
          <span>{revision ? `Selected r${revision.revision_number}` : 'No published revision'}</span>
        </div>
        <textarea className="topology-json-editor" spellCheck={false}
          aria-label="Topology JSON document" value={source}
          onChange={(event) => { setSource(event.target.value); setError(''); setStatus('') }} />
        <div className={`topology-json-review${review.issues.length ? ' is-invalid' : ' is-valid'}`}>
          <div>
            <strong>{review.issues.length ? 'Client review found issues' : 'Document shape is ready for publication'}</strong>
            {review.summary && (
              <span>
                {review.summary.componentCount} components · {review.summary.assemblyCount} assemblies ·{' '}
                {review.summary.connectionCount} connections · {review.summary.mediumCount} fluids ·{' '}
                {review.summary.materialCount} materials
              </span>
            )}
          </div>
          {changesModelIdentity && (
            <p>Model identity changes from <code>{currentModelId}</code> to <code>{review.summary?.modelId}</code>.</p>
          )}
          {review.issues.length > 0 && (
            <ul>{review.issues.slice(0, 12).map((issue) => <li key={issue}>{issue}</li>)}</ul>
          )}
          <small>
            This review checks the public document shape. Publication invokes the
            authoritative service parser, registry validation, canonicalization, and checksum.
          </small>
        </div>
        {(error || status) && <div className={`form-error topology-json-message${error ? '' : ' is-status'}`}>
          {error || status}
        </div>}
        <footer>
          <button type="button" className="secondary-button" onClick={onCancel}>Cancel</button>
          <button type="submit" className="primary-button"
            disabled={publishing || !review.document}>
            {publishing ? 'Publishing…' : revision ? 'Publish child revision' : 'Publish first revision'}
          </button>
        </footer>
      </form>
    </div>
  )
}
