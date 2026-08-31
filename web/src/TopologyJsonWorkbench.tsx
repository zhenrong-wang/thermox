import { useMemo, useRef, useState, type ChangeEvent, type FormEvent } from 'react'
import { reviewTopologyJson, topologyJsonText } from './topologyJson'
import {
  reviewTopologyDraftSource,
  topologyDraftDefinition,
  topologyDraftSourceText,
  type TopologyDraftDefinition,
} from './topologyDraft'
import type {
  ArtifactRevision,
  ModelRevision,
  TopologyDocument,
  TopologyDraftPromotionReview,
} from './types'

interface TopologyJsonWorkbenchProps {
  topology?: TopologyDocument
  revision?: ModelRevision
  drafts: ArtifactRevision[]
  publishing: boolean
  onCancel: () => void
  onPublish: (document: TopologyDocument) => Promise<void>
  onLoadDraft: (revision: ArtifactRevision) => Promise<TopologyDraftDefinition>
  onSaveDraft: (
    definition: TopologyDraftDefinition,
    parentArtifactRevisionId: string,
  ) => Promise<ArtifactRevision>
  onReviewDraft: (revision: ArtifactRevision) => Promise<TopologyDraftPromotionReview>
  onPromoteDraft: (revision: ArtifactRevision) => Promise<void>
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
  drafts,
  publishing,
  onCancel,
  onPublish,
  onLoadDraft,
  onSaveDraft,
  onReviewDraft,
  onPromoteDraft,
}: TopologyJsonWorkbenchProps) {
  const selectedText = topologyJsonText(topology ?? emptyDocument())
  const [source, setSource] = useState(selectedText)
  const [status, setStatus] = useState('')
  const [error, setError] = useState('')
  const [draftId, setDraftId] = useState(
    `draft-${topology?.model.id ?? 'thermal-system'}`,
  )
  const [draftLabel, setDraftLabel] = useState('')
  const [selectedDraftRevisionId, setSelectedDraftRevisionId] = useState('')
  const [activeDraft, setActiveDraft] = useState<ArtifactRevision>()
  const [activeDraftSource, setActiveDraftSource] = useState('')
  const [activeDraftLabel, setActiveDraftLabel] = useState('')
  const [serverReview, setServerReview] =
    useState<TopologyDraftPromotionReview>()
  const [savingDraft, setSavingDraft] = useState(false)
  const fileInput = useRef<HTMLInputElement>(null)
  const review = useMemo(() => reviewTopologyJson(source), [source])
  const draftReview = useMemo(() => reviewTopologyDraftSource(source), [source])
  const currentModelId = topology?.model.id
  const changesModelIdentity = Boolean(
    currentModelId && review.summary?.modelId &&
    currentModelId !== review.summary.modelId,
  )
  const exactActiveDraft = Boolean(
    activeDraft && source === activeDraftSource &&
    draftId === activeDraft.artifact_id && draftLabel === activeDraftLabel,
  )

  async function loadFile(event: ChangeEvent<HTMLInputElement>) {
    const file = event.target.files?.[0]
    if (!file) return
    setError('')
    setStatus('')
    try {
      setSource(await file.text())
      setActiveDraft(undefined)
      setServerReview(undefined)
      setSelectedDraftRevisionId('')
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

  async function loadDraft() {
    const revision = drafts.find(
      (candidate) => candidate.artifact_revision_id === selectedDraftRevisionId,
    )
    if (!revision) return
    setError('')
    setStatus('')
    setServerReview(undefined)
    try {
      const definition = await onLoadDraft(revision)
      const draftSource = topologyDraftSourceText(definition)
      setSource(draftSource)
      setDraftId(definition.id)
      setDraftLabel(definition.label ?? '')
      setActiveDraft(revision)
      setActiveDraftSource(draftSource)
      setActiveDraftLabel(definition.label ?? '')
      const authoritative = await onReviewDraft(revision)
      setServerReview(authoritative)
      setStatus(
        `Loaded ${definition.id} draft r${revision.revision_number}; service review ${authoritative.promotable ? 'passed' : 'found blockers'}.`,
      )
    } catch (reason) {
      setError(reason instanceof Error ? reason.message : 'Could not load the draft revision.')
    }
  }

  async function saveDraft() {
    const id = draftId.trim()
    if (!id || !draftReview.document) return
    setSavingDraft(true)
    setError('')
    setStatus('')
    setServerReview(undefined)
    try {
      const revision = await onSaveDraft(
        topologyDraftDefinition(id, draftLabel, draftReview.document),
        activeDraft?.artifact_id === id
          ? activeDraft.artifact_revision_id
          : '',
      )
      setActiveDraft(revision)
      setActiveDraftSource(source)
      setActiveDraftLabel(draftLabel)
      setSelectedDraftRevisionId(revision.artifact_revision_id)
      const authoritative = await onReviewDraft(revision)
      setServerReview(authoritative)
      setStatus(
        `Saved immutable draft ${revision.artifact_id} r${revision.revision_number}; service review ${authoritative.promotable ? 'passed' : 'found blockers'}.`,
      )
    } catch (reason) {
      setError(reason instanceof Error ? reason.message : 'Draft publication was rejected.')
    } finally {
      setSavingDraft(false)
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
    if (exactActiveDraft) {
      if (!activeDraft || !serverReview?.promotable) return
    } else if (!review.document) return
    setError('')
    setStatus('')
    try {
      if (exactActiveDraft && activeDraft) await onPromoteDraft(activeDraft)
      else if (review.document) await onPublish(review.document)
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
            onClick={() => {
              setSource(selectedText)
              setActiveDraft(undefined)
              setServerReview(undefined)
              setSelectedDraftRevisionId('')
              setError('')
              setStatus('Restored the selected revision.')
            }}>
            Restore selected
          </button>
          <button type="button" className="secondary-button" disabled={!topology}
            onClick={() => void copySelected()}>Copy selected JSON</button>
          <button type="button" className="secondary-button" disabled={!topology}
            onClick={downloadSelected}>Download selected JSON</button>
          <span>{revision ? `Selected r${revision.revision_number}` : 'No published revision'}</span>
        </div>
        <div className="topology-draft-bar">
          <label>
            <span>Draft ID</span>
            <input value={draftId} onChange={(event) => {
              setDraftId(event.target.value)
              if (event.target.value !== activeDraft?.artifact_id) setActiveDraft(undefined)
            }} />
          </label>
          <label>
            <span>Label</span>
            <input value={draftLabel} placeholder="Optional"
              onChange={(event) => setDraftLabel(event.target.value)} />
          </label>
          <label className="topology-draft-picker">
            <span>Saved draft revision</span>
            <select value={selectedDraftRevisionId}
              onChange={(event) => setSelectedDraftRevisionId(event.target.value)}>
              <option value="">Select…</option>
              {drafts.map((candidate) => (
                <option key={candidate.artifact_revision_id}
                  value={candidate.artifact_revision_id}>
                  {candidate.artifact_id} · r{candidate.revision_number}
                </option>
              ))}
            </select>
          </label>
          <button type="button" className="secondary-button"
            disabled={!selectedDraftRevisionId} onClick={() => void loadDraft()}>
            Load draft
          </button>
        </div>
        <textarea className="topology-json-editor" spellCheck={false}
          aria-label="Topology JSON document" value={source}
          onChange={(event) => { setSource(event.target.value); setError(''); setStatus('') }} />
        <div className={`topology-json-review${review.issues.length ? ' is-invalid' : ' is-valid'}`}>
          <div>
            <strong>{draftReview.syntaxIssue
              ? 'JSON must be corrected before it can be saved'
              : review.issues.length
                ? `Draft-saveable · ${review.issues.length} promotion blockers`
                : 'Document shape is ready for topology publication'}</strong>
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
          {draftReview.syntaxIssue && <p>{draftReview.syntaxIssue}</p>}
          {!draftReview.syntaxIssue && review.issues.length > 0 && (
            <ul>{review.issues.slice(0, 12).map((issue) => <li key={issue}>{issue}</li>)}</ul>
          )}
          <small>
            Any valid JSON object can be stored as an immutable draft. Promotion invokes the
            strict service parser, canonicalization, and checksum; calculation readiness is
            validated later against the registry, case, and pinned artifacts.
          </small>
          {exactActiveDraft && serverReview && (
            <p className={serverReview.promotable ? '' : 'is-invalid'}>
              Service review: {serverReview.promotable
                ? `promotable as ${serverReview.model_id}; checksum ${serverReview.artifact_checksum}`
                : serverReview.issues.map((issue) => issue.message).join('; ')}
            </p>
          )}
        </div>
        {(error || status) && <div className={`form-error topology-json-message${error ? '' : ' is-status'}`}>
          {error || status}
        </div>}
        <footer>
          <button type="button" className="secondary-button" onClick={onCancel}>Cancel</button>
          <button type="button" className="secondary-button"
            disabled={savingDraft || !draftId.trim() || !draftReview.document}
            onClick={() => void saveDraft()}>
            {savingDraft ? 'Saving draft…' : activeDraft ? 'Save child draft' : 'Save draft'}
          </button>
          <button type="submit" className="primary-button"
            disabled={publishing || (exactActiveDraft
              ? !serverReview?.promotable
              : !review.document)}>
            {publishing
              ? 'Publishing…'
              : exactActiveDraft
                ? 'Promote reviewed draft'
                : revision ? 'Publish child revision' : 'Publish first revision'}
          </button>
        </footer>
      </form>
    </div>
  )
}
