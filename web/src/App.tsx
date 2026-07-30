import { useEffect, useMemo, useState } from 'react'
import type { Connection } from '@xyflow/react'
import { api, errorMessage, isAbortError } from './api'
import { CaseCreateForm } from './CaseCreateForm'
import { CaseRevisionPanel } from './CaseRevisionPanel'
import { CaseWorkspace } from './CaseWorkspace'
import { ComponentForm } from './ComponentForm'
import { ConnectionForm } from './ConnectionForm'
import {
  buildConnectionOperation,
  type ConnectionIntent,
} from './graphAuthoring'
import {
  InspectorPanel,
  type GraphSelection,
} from './InspectorPanel'
import { MediumForm } from './MediumForm'
import { RunConfigurationForm } from './RunConfigurationForm'
import { RunConfigurationPanel } from './RunConfigurationPanel'
import { RunConfigurationWorkspace } from './RunConfigurationWorkspace'
import { ResultSelectionPanel } from './ResultSelectionPanel'
import { ResultsWorkspace } from './ResultsWorkspace'
import { TopologyCanvas } from './TopologyCanvas'
import type {
  ArtifactRevision,
  Catalog,
  CatalogComponent,
  CaseDocument,
  CaseEditOperation,
  CaseRevision,
  ComponentDefinition,
  ConnectionDefinition,
  CreateRunConfiguration,
  GraphEditOperation,
  MediumDefinition,
  ModelRevision,
  ProjectModelValidation,
  Project,
  RunConfigurationRevision,
  SimulationJob,
  SimulationResult,
  SimulationJobState,
  TopologyDocument,
} from './types'

function shortKind(kind: string): string {
  const parts = kind.split('.')
  return parts.length > 2 ? `${parts[0]}.${parts[1]}` : kind
}

function App() {
  const [workspaceView, setWorkspaceView] =
    useState<'topology' | 'cases' | 'runs' | 'results'>('topology')
  const [catalog, setCatalog] = useState<Catalog>()
  const [projects, setProjects] = useState<Project[]>([])
  const [selectedProjectId, setSelectedProjectId] = useState('')
  const [revisions, setRevisions] = useState<ModelRevision[]>([])
  const [artifactRevisions, setArtifactRevisions] = useState<
    ArtifactRevision[]
  >([])
  const [caseRevisions, setCaseRevisions] = useState<CaseRevision[]>([])
  const [selectedCaseRevisionId, setSelectedCaseRevisionId] = useState('')
  const [selectedCaseRevision, setSelectedCaseRevision] =
    useState<CaseRevision>()
  const [selectedRevisionId, setSelectedRevisionId] = useState('')
  const [topology, setTopology] = useState<TopologyDocument>()
  const [filter, setFilter] = useState('')
  const [error, setError] = useState('')
  const [loading, setLoading] = useState(true)
  const [publishing, setPublishing] = useState(false)
  const [operationError, setOperationError] = useState('')
  const [operationStatus, setOperationStatus] = useState('')
  const [newComponentType, setNewComponentType] =
    useState<CatalogComponent>()
  const [editingComponent, setEditingComponent] =
    useState<ComponentDefinition>()
  const [editingConnection, setEditingConnection] =
    useState<ConnectionDefinition>()
  const [selection, setSelection] = useState<GraphSelection>()
  const [addingMedium, setAddingMedium] = useState(false)
  const [addingCase, setAddingCase] = useState(false)
  const [casePublishing, setCasePublishing] = useState(false)
  const [caseOperationError, setCaseOperationError] = useState('')
  const [caseOperationStatus, setCaseOperationStatus] = useState('')
  const [validationResult, setValidationResult] =
    useState<ProjectModelValidation>()
  const [validating, setValidating] = useState(false)
  const [runConfigurationRevisions, setRunConfigurationRevisions] = useState<
    RunConfigurationRevision[]
  >([])
  const [
    selectedRunConfigurationRevisionId,
    setSelectedRunConfigurationRevisionId,
  ] = useState('')
  const [selectedRunConfiguration, setSelectedRunConfiguration] =
    useState<RunConfigurationRevision>()
  const [runPublishing, setRunPublishing] = useState(false)
  const [runOperationError, setRunOperationError] = useState('')
  const [runOperationStatus, setRunOperationStatus] = useState('')
  const [addingRunConfiguration, setAddingRunConfiguration] = useState(false)
  const [revisingRunConfiguration, setRevisingRunConfiguration] =
    useState(false)
  const [simulationJobs, setSimulationJobs] = useState<SimulationJob[]>([])
  const [selectedSimulationJobId, setSelectedSimulationJobId] = useState('')
  const [jobsLoading, setJobsLoading] = useState(false)
  const [jobSubmitting, setJobSubmitting] = useState(false)
  const [jobStateFilter, setJobStateFilter] =
    useState<'' | SimulationJobState>('')
  const [jobsNextCursor, setJobsNextCursor] = useState<string | null>(null)
  const [submissionIdempotencyKey, setSubmissionIdempotencyKey] = useState('')
  const [resultJobs, setResultJobs] = useState<SimulationJob[]>([])
  const [selectedResultJobId, setSelectedResultJobId] = useState('')
  const [resultJobsLoading, setResultJobsLoading] = useState(false)
  const [simulationResult, setSimulationResult] = useState<SimulationResult>()
  const [resultLoading, setResultLoading] = useState(false)
  const [resultError, setResultError] = useState('')

  useEffect(() => {
    setSubmissionIdempotencyKey('')
  }, [selectedProjectId, selectedRunConfigurationRevisionId])

  useEffect(() => {
    const controller = new AbortController()
    Promise.all([
      api.catalog(controller.signal),
      api.projects(controller.signal),
    ])
      .then(([catalogResponse, projectResponse]) => {
        setError('')
        setCatalog(catalogResponse)
        setProjects(projectResponse.projects)
        setSelectedProjectId(projectResponse.projects[0]?.project_id ?? '')
      })
      .catch((reason: unknown) => {
        if (!isAbortError(reason)) setError(errorMessage(reason))
      })
      .finally(() => setLoading(false))
    return () => controller.abort()
  }, [])

  useEffect(() => {
    setRunConfigurationRevisions([])
    setSelectedRunConfigurationRevisionId('')
    setSelectedRunConfiguration(undefined)
    if (!selectedProjectId) return
    const controller = new AbortController()
    api
      .runConfigurationRevisions(selectedProjectId, controller.signal)
      .then((response) => {
        setRunOperationError('')
        setRunConfigurationRevisions(
          [...response.run_configuration_revisions].sort(
            (left, right) =>
              right.created_at_epoch_ms - left.created_at_epoch_ms,
          ),
        )
      })
      .catch((reason: unknown) => {
        if (!isAbortError(reason)) setRunOperationError(errorMessage(reason))
      })
    return () => controller.abort()
  }, [selectedProjectId])

  useEffect(() => {
    setRevisions([])
    setArtifactRevisions([])
    setSelectedRevisionId('')
    setTopology(undefined)
    if (!selectedProjectId) return
    const controller = new AbortController()
    Promise.all([
      api.modelRevisions(selectedProjectId, controller.signal),
      api.artifactRevisions(selectedProjectId, controller.signal),
    ])
      .then(([response, artifacts]) => {
        setError('')
        const ordered = [...response.model_revisions].sort(
          (left, right) => right.revision_number - left.revision_number,
        )
        setRevisions(ordered)
        setArtifactRevisions(artifacts.artifact_revisions)
        setSelectedRevisionId(ordered[0]?.model_revision_id ?? '')
      })
      .catch((reason: unknown) => {
        if (!isAbortError(reason)) setError(errorMessage(reason))
      })
    return () => controller.abort()
  }, [selectedProjectId])

  useEffect(() => {
    setTopology(undefined)
    setSelection(undefined)
    if (!selectedProjectId || !selectedRevisionId) return
    const controller = new AbortController()
    api
      .modelRevision(
        selectedProjectId,
        selectedRevisionId,
        controller.signal,
      )
      .then((response) => {
        setError('')
        setTopology(response.model)
      })
      .catch((reason: unknown) => {
        if (!isAbortError(reason)) setError(errorMessage(reason))
      })
    return () => controller.abort()
  }, [selectedProjectId, selectedRevisionId])

  useEffect(() => {
    setCaseRevisions([])
    setSelectedCaseRevisionId('')
    setSelectedCaseRevision(undefined)
    if (!selectedProjectId || !selectedRevisionId) return
    const controller = new AbortController()
    api
      .caseRevisions(
        selectedProjectId,
        selectedRevisionId,
        controller.signal,
      )
      .then((response) => {
        setCaseOperationError('')
        const ordered = [...response.case_revisions].sort(
          (left, right) =>
            right.created_at_epoch_ms - left.created_at_epoch_ms,
        )
        setCaseRevisions(ordered)
        setSelectedCaseRevisionId(ordered[0]?.case_revision_id ?? '')
      })
      .catch((reason: unknown) => {
        if (!isAbortError(reason)) setCaseOperationError(errorMessage(reason))
      })
    return () => controller.abort()
  }, [selectedProjectId, selectedRevisionId])

  useEffect(() => {
    setSelectedCaseRevision(undefined)
    setValidationResult(undefined)
    if (
      !selectedProjectId ||
      !selectedRevisionId ||
      !selectedCaseRevisionId
    ) {
      return
    }
    const controller = new AbortController()
    api
      .caseRevision(
        selectedProjectId,
        selectedRevisionId,
        selectedCaseRevisionId,
        controller.signal,
      )
      .then((revision) => {
        setCaseOperationError('')
        setSelectedCaseRevision(revision)
      })
      .catch((reason: unknown) => {
        if (!isAbortError(reason)) setCaseOperationError(errorMessage(reason))
      })
    return () => controller.abort()
  }, [selectedProjectId, selectedRevisionId, selectedCaseRevisionId])

  const selectedProject = projects.find(
    (project) => project.project_id === selectedProjectId,
  )
  const selectedRevision = revisions.find(
    (revision) => revision.model_revision_id === selectedRevisionId,
  )
  const selectedResultJob = resultJobs.find(
    (job) => job.job_id === selectedResultJobId,
  )
  const palette = useMemo(() => {
    const query = filter.trim().toLowerCase()
    return (catalog?.components ?? []).filter(
      (component) =>
        !query ||
        component.kind.toLowerCase().includes(query) ||
        component.ports.some((port) =>
          port.domain.toLowerCase().includes(query),
        ),
    )
  }, [catalog, filter])
  const requiredArtifactIds = useMemo(
    () =>
      [
        ...new Set(
          (topology?.model.components ?? []).flatMap((component) =>
            Object.values(component.artifacts ?? {}),
          ),
        ),
      ].sort(),
    [topology],
  )
  const visibleRunConfigurations = useMemo(
    () =>
      runConfigurationRevisions.filter(
        (revision) => revision.model_revision_id === selectedRevisionId,
      ),
    [runConfigurationRevisions, selectedRevisionId],
  )
  const selectedArtifactRevisionIds = useMemo(
    () =>
      requiredArtifactIds
        .map((artifactId) => {
          const validated = validationResult?.artifact_revisions.find(
            (revision) => revision.artifact_id === artifactId,
          )
          if (validated) return validated.artifact_revision_id
          return artifactRevisions
            .filter((revision) => revision.artifact_id === artifactId)
            .sort(
              (left, right) =>
                right.revision_number - left.revision_number,
            )[0]?.artifact_revision_id
        })
        .filter((id): id is string => Boolean(id)),
    [artifactRevisions, requiredArtifactIds, validationResult],
  )

  useEffect(() => {
    const selectedStillVisible = visibleRunConfigurations.some(
      (revision) =>
        revision.run_configuration_revision_id ===
        selectedRunConfigurationRevisionId,
    )
    if (!selectedStillVisible) {
      setSelectedRunConfigurationRevisionId(
        visibleRunConfigurations[0]?.run_configuration_revision_id ?? '',
      )
    }
  }, [selectedRunConfigurationRevisionId, visibleRunConfigurations])

  useEffect(() => {
    setSelectedRunConfiguration(undefined)
    if (!selectedProjectId || !selectedRunConfigurationRevisionId) return
    const controller = new AbortController()
    api
      .runConfigurationRevision(
        selectedProjectId,
        selectedRunConfigurationRevisionId,
        controller.signal,
      )
      .then((revision) => {
        setRunOperationError('')
        setSelectedRunConfiguration(revision)
      })
      .catch((reason: unknown) => {
        if (!isAbortError(reason)) setRunOperationError(errorMessage(reason))
      })
    return () => controller.abort()
  }, [selectedProjectId, selectedRunConfigurationRevisionId])

  useEffect(() => {
    setSimulationJobs([])
    setSelectedSimulationJobId('')
    setJobsNextCursor(null)
    if (!selectedProjectId || !selectedRunConfigurationRevisionId) return
    const controller = new AbortController()
    setJobsLoading(true)
    api
      .simulationJobs(
        selectedProjectId,
        selectedRunConfigurationRevisionId,
        jobStateFilter || undefined,
        undefined,
        controller.signal,
      )
      .then((page) => {
        setRunOperationError('')
        setSimulationJobs(page.jobs)
        setSelectedSimulationJobId(page.jobs[0]?.job_id ?? '')
        setJobsNextCursor(page.next_cursor)
      })
      .catch((reason: unknown) => {
        if (!isAbortError(reason)) setRunOperationError(errorMessage(reason))
      })
      .finally(() => setJobsLoading(false))
    return () => controller.abort()
  }, [
    jobStateFilter,
    selectedProjectId,
    selectedRunConfigurationRevisionId,
  ])

  const activeJobCount = simulationJobs.filter(
    (job) => job.state === 'queued' || job.state === 'running',
  ).length

  useEffect(() => {
    if (
      workspaceView !== 'runs' ||
      !selectedProjectId ||
      !selectedRunConfigurationRevisionId ||
      activeJobCount === 0
    ) {
      return
    }
    const timer = window.setInterval(() => {
      void api
        .simulationJobs(
          selectedProjectId,
          selectedRunConfigurationRevisionId,
          jobStateFilter || undefined,
        )
        .then((page) => {
          setSimulationJobs(page.jobs)
          setJobsNextCursor(page.next_cursor)
        })
        .catch((reason: unknown) => {
          setRunOperationError(errorMessage(reason))
        })
    }, 4000)
    return () => window.clearInterval(timer)
  }, [
    activeJobCount,
    jobStateFilter,
    selectedProjectId,
    selectedRunConfigurationRevisionId,
    workspaceView,
  ])

  useEffect(() => {
    if (workspaceView !== 'results') return
    setResultJobs([])
    setSelectedResultJobId('')
    setSimulationResult(undefined)
    setResultError('')
    if (!selectedProjectId || !selectedRunConfigurationRevisionId) return
    const controller = new AbortController()
    setResultJobsLoading(true)
    api
      .simulationJobs(
        selectedProjectId,
        selectedRunConfigurationRevisionId,
        'succeeded',
        undefined,
        controller.signal,
      )
      .then((page) => {
        setResultJobs(page.jobs)
        setSelectedResultJobId(
          page.jobs.some((job) => job.job_id === selectedSimulationJobId)
            ? selectedSimulationJobId
            : page.jobs[0]?.job_id ?? '',
        )
      })
      .catch((reason: unknown) => {
        if (!isAbortError(reason)) setResultError(errorMessage(reason))
      })
      .finally(() => {
        if (!controller.signal.aborted) setResultJobsLoading(false)
      })
    return () => controller.abort()
  }, [
    selectedProjectId,
    selectedRunConfigurationRevisionId,
    selectedSimulationJobId,
    workspaceView,
  ])

  useEffect(() => {
    setSimulationResult(undefined)
    setResultError('')
    if (workspaceView !== 'results' || !selectedResultJobId) return
    const controller = new AbortController()
    setResultLoading(true)
    api
      .simulationResult(selectedResultJobId, controller.signal)
      .then((result) => {
        setResultError('')
        setSimulationResult(result)
      })
      .catch((reason: unknown) => {
        if (!isAbortError(reason)) setResultError(errorMessage(reason))
      })
      .finally(() => {
        if (!controller.signal.aborted) setResultLoading(false)
      })
    return () => controller.abort()
  }, [selectedResultJobId, workspaceView])

  async function publishEdits(
    operations: GraphEditOperation[],
    successMessage: string,
  ) {
    if (!selectedProjectId || !selectedRevisionId) {
      throw new Error('Select a topology revision before editing.')
    }
    setPublishing(true)
    setOperationError('')
    setOperationStatus('')
    try {
      const child = await api.applyGraphEdits(
        selectedProjectId,
        selectedRevisionId,
        operations,
      )
      setRevisions((current) => [
        child,
        ...current.filter(
          (item) => item.model_revision_id !== child.model_revision_id,
        ),
      ])
      setSelectedRevisionId(child.model_revision_id)
      setOperationStatus(
        `${successMessage} Published revision r${child.revision_number}.`,
      )
      return child
    } catch (reason) {
      const message = errorMessage(reason)
      setOperationError(message)
      throw new Error(message)
    } finally {
      setPublishing(false)
    }
  }

  async function addComponent(component: ComponentDefinition) {
    await publishEdits(
      [
        {
          action: 'upsert',
          entity_type: 'component',
          entity_id: component.id,
          entity: { ...component },
        },
      ],
      `Added ${component.id}.`,
    )
    setNewComponentType(undefined)
  }

  async function addMedium(medium: MediumDefinition) {
    await publishEdits(
      [
        {
          action: 'upsert',
          entity_type: 'medium',
          entity_id: medium.id,
          entity: { ...medium },
        },
      ],
      `Added fluid ${medium.id}.`,
    )
    setAddingMedium(false)
  }

  async function createCase(document: CaseDocument) {
    if (!selectedProjectId || !selectedRevisionId) {
      throw new Error('Select a topology revision before creating a case.')
    }
    setCasePublishing(true)
    setCaseOperationError('')
    setCaseOperationStatus('')
    try {
      const revision = await api.createCaseRevision(
        selectedProjectId,
        selectedRevisionId,
        document,
      )
      setCaseRevisions((current) => [
        revision,
        ...current.filter(
          (item) => item.case_revision_id !== revision.case_revision_id,
        ),
      ])
      setSelectedCaseRevision(revision)
      setSelectedCaseRevisionId(revision.case_revision_id)
      setCaseOperationStatus(
        `Created ${revision.case_id} r${revision.revision_number}.`,
      )
      setAddingCase(false)
    } catch (reason) {
      const message = errorMessage(reason)
      setCaseOperationError(message)
      throw new Error(message)
    } finally {
      setCasePublishing(false)
    }
  }

  async function editCase(
    operations: CaseEditOperation[],
    successMessage: string,
  ) {
    if (
      !selectedProjectId ||
      !selectedRevisionId ||
      !selectedCaseRevisionId
    ) {
      throw new Error('Select a case revision before editing.')
    }
    setCasePublishing(true)
    setCaseOperationError('')
    setCaseOperationStatus('')
    try {
      const child = await api.applyCaseEdits(
        selectedProjectId,
        selectedRevisionId,
        selectedCaseRevisionId,
        operations,
      )
      setCaseRevisions((current) => [
        child,
        ...current.filter(
          (item) => item.case_revision_id !== child.case_revision_id,
        ),
      ])
      setSelectedCaseRevision(child)
      setSelectedCaseRevisionId(child.case_revision_id)
      setCaseOperationStatus(
        `${successMessage} Published case r${child.revision_number}.`,
      )
    } catch (reason) {
      const message = errorMessage(reason)
      setCaseOperationError(message)
      throw new Error(message)
    } finally {
      setCasePublishing(false)
    }
  }

  async function validateCase(artifactRevisionIds: string[]) {
    if (
      !selectedProjectId ||
      !selectedRevisionId ||
      !selectedCaseRevisionId
    ) {
      throw new Error('Select a case revision before validation.')
    }
    setValidating(true)
    setCaseOperationError('')
    setCaseOperationStatus('')
    try {
      const result = await api.validateCaseRevision(
        selectedProjectId,
        selectedRevisionId,
        selectedCaseRevisionId,
        artifactRevisionIds,
      )
      setValidationResult(result)
      setCaseOperationStatus(
        result.validation.compilation.compiled
          ? 'Exact revision set compiled successfully.'
          : 'Validation completed with compiler diagnostics.',
      )
    } catch (reason) {
      const message = errorMessage(reason)
      setCaseOperationError(message)
      throw new Error(message)
    } finally {
      setValidating(false)
    }
  }

  function beginCreateRunConfiguration() {
    if (!selectedCaseRevision) {
      setRunOperationError(
        'Select or create an operating case for this topology revision first.',
      )
      return
    }
    if (selectedArtifactRevisionIds.length !== requiredArtifactIds.length) {
      setRunOperationError(
        'Resolve every component artifact to an immutable project revision first.',
      )
      return
    }
    setRunOperationError('')
    setAddingRunConfiguration(true)
  }

  async function createRunConfiguration(request: CreateRunConfiguration) {
    if (!selectedProjectId) {
      throw new Error('Select a project before creating a run configuration.')
    }
    setRunPublishing(true)
    setRunOperationError('')
    setRunOperationStatus('')
    try {
      const revision = await api.createRunConfigurationRevision(
        selectedProjectId,
        request,
      )
      setRunConfigurationRevisions((current) => [
        revision,
        ...current.filter(
          (item) =>
            item.run_configuration_revision_id !==
            revision.run_configuration_revision_id,
        ),
      ])
      setSelectedRunConfiguration(revision)
      setSelectedRunConfigurationRevisionId(
        revision.run_configuration_revision_id,
      )
      setRunOperationStatus(
        `Published ${revision.run_configuration_id} r${revision.revision_number}.`,
      )
      setAddingRunConfiguration(false)
      setRevisingRunConfiguration(false)
    } catch (reason) {
      const message = errorMessage(reason)
      setRunOperationError(message)
      throw new Error(message)
    } finally {
      setRunPublishing(false)
    }
  }

  async function refreshSimulationJobs() {
    if (!selectedProjectId || !selectedRunConfigurationRevisionId) return
    setJobsLoading(true)
    try {
      const page = await api.simulationJobs(
        selectedProjectId,
        selectedRunConfigurationRevisionId,
        jobStateFilter || undefined,
      )
      setRunOperationError('')
      setSimulationJobs(page.jobs)
      setJobsNextCursor(page.next_cursor)
      setSelectedSimulationJobId((current) =>
        page.jobs.some((job) => job.job_id === current)
          ? current
          : page.jobs[0]?.job_id ?? '',
      )
    } catch (reason) {
      setRunOperationError(errorMessage(reason))
    } finally {
      setJobsLoading(false)
    }
  }

  async function loadMoreSimulationJobs() {
    if (
      !selectedProjectId ||
      !selectedRunConfigurationRevisionId ||
      !jobsNextCursor
    ) {
      return
    }
    setJobsLoading(true)
    try {
      const page = await api.simulationJobs(
        selectedProjectId,
        selectedRunConfigurationRevisionId,
        jobStateFilter || undefined,
        jobsNextCursor,
      )
      setSimulationJobs((current) => [
        ...current,
        ...page.jobs.filter(
          (job) => !current.some((item) => item.job_id === job.job_id),
        ),
      ])
      setJobsNextCursor(page.next_cursor)
    } catch (reason) {
      setRunOperationError(errorMessage(reason))
    } finally {
      setJobsLoading(false)
    }
  }

  async function submitSimulationJob() {
    if (!selectedProjectId || !selectedRunConfigurationRevisionId) return
    const idempotencyKey =
      submissionIdempotencyKey ||
      `web-${selectedRunConfigurationRevisionId}-${crypto.randomUUID()}`
    setSubmissionIdempotencyKey(idempotencyKey)
    setJobSubmitting(true)
    setRunOperationError('')
    try {
      const job = await api.submitSimulation(
        selectedProjectId,
        selectedRunConfigurationRevisionId,
        idempotencyKey,
      )
      setSubmissionIdempotencyKey('')
      setJobStateFilter('')
      setSimulationJobs((current) => [
        job,
        ...current.filter((item) => item.job_id !== job.job_id),
      ])
      setSelectedSimulationJobId(job.job_id)
      setRunOperationStatus(`Queued execution ${job.job_id}.`)
    } catch (reason) {
      setRunOperationError(errorMessage(reason))
    } finally {
      setJobSubmitting(false)
    }
  }

  async function cancelSimulationJob(job: SimulationJob) {
    if (
      !window.confirm(
        `Cancel ${job.job_id} at job revision ${job.revision}?`,
      )
    ) {
      return
    }
    setRunOperationError('')
    try {
      const cancelled = await api.cancelSimulation(job.job_id, job.revision)
      setSimulationJobs((current) =>
        jobStateFilter && jobStateFilter !== cancelled.state
          ? current.filter((item) => item.job_id !== cancelled.job_id)
          : current.map((item) =>
              item.job_id === cancelled.job_id ? cancelled : item,
            ),
      )
      if (jobStateFilter && jobStateFilter !== cancelled.state) {
        setSelectedSimulationJobId('')
      }
      setRunOperationStatus(`Cancelled ${cancelled.job_id}.`)
    } catch (reason) {
      setRunOperationError(errorMessage(reason))
    }
  }

  async function refreshResultJobs() {
    if (!selectedProjectId || !selectedRunConfigurationRevisionId) return
    setResultJobsLoading(true)
    setResultError('')
    try {
      const page = await api.simulationJobs(
        selectedProjectId,
        selectedRunConfigurationRevisionId,
        'succeeded',
      )
      setResultJobs(page.jobs)
      setSelectedResultJobId((current) =>
        page.jobs.some((job) => job.job_id === current)
          ? current
          : page.jobs[0]?.job_id ?? '',
      )
    } catch (reason) {
      setResultError(errorMessage(reason))
    } finally {
      setResultJobsLoading(false)
    }
  }

  async function retryResult() {
    if (!selectedResultJobId) return
    setResultLoading(true)
    setResultError('')
    try {
      setSimulationResult(await api.simulationResult(selectedResultJobId))
    } catch (reason) {
      setResultError(errorMessage(reason))
    } finally {
      setResultLoading(false)
    }
  }

  async function updateComponent(component: ComponentDefinition) {
    await publishEdits(
      [
        {
          action: 'upsert',
          entity_type: 'component',
          entity_id: component.id,
          entity: { ...component },
        },
      ],
      `Updated ${component.id}.`,
    )
    setEditingComponent(undefined)
  }

  async function updateConnection(
    connection: ConnectionIntent,
    existing: ConnectionDefinition,
  ) {
    if (!topology || !catalog) {
      throw new Error('Topology and runtime catalog are required.')
    }
    const operation = buildConnectionOperation(
      connection,
      topology,
      catalog,
      existing.id,
    )
    await publishEdits(
      [operation],
      `Updated connection ${existing.id}.`,
    )
    setEditingConnection(undefined)
  }

  async function removeComponent(component: ComponentDefinition) {
    if (
      !window.confirm(
        `Remove ${component.id}? Attached connections will also be removed in the new revision.`,
      )
    ) {
      return
    }
    try {
      await publishEdits(
        [
          {
            action: 'remove',
            entity_type: 'component',
            entity_id: component.id,
            cascade: true,
          },
        ],
        `Removed ${component.id}.`,
      )
    } catch {
      // publishEdits exposes the service diagnostic in the workspace.
    }
  }

  async function removeConnection(connection: ConnectionDefinition) {
    if (!window.confirm(`Remove connection ${connection.id}?`)) return
    try {
      await publishEdits(
        [
          {
            action: 'remove',
            entity_type: 'connection',
            entity_id: connection.id,
          },
        ],
        `Removed connection ${connection.id}.`,
      )
    } catch {
      // publishEdits exposes the service diagnostic in the workspace.
    }
  }

  async function connectPorts(connection: Connection) {
    if (
      !topology ||
      !connection.source ||
      !connection.target ||
      !connection.sourceHandle ||
      !connection.targetHandle
    ) {
      setOperationError('A connection requires two concrete component ports.')
      return
    }
    if (!catalog) {
      setOperationError('The runtime catalog is not loaded.')
      return
    }
    try {
      const operation = buildConnectionOperation(
        connection,
        topology,
        catalog,
      )
      await publishEdits(
        [operation],
        `Connected ${connection.source}.${connection.sourceHandle} to ${connection.target}.${connection.targetHandle}.`,
      )
    } catch (reason) {
      setOperationError(errorMessage(reason))
    }
  }

  return (
    <main className="app-shell">
      <header className="topbar">
        <div className="brand">
          <div className="brand-mark" aria-hidden="true">
            <span />
            <span />
            <span />
          </div>
          <div>
            <strong>thermox</strong>
            <small>system workspace</small>
          </div>
        </div>
        <div className="context">
          <span className="context-label">Project</span>
          <select
            aria-label="Project"
            value={selectedProjectId}
            onChange={(event) => setSelectedProjectId(event.target.value)}
          >
            {projects.map((project) => (
              <option key={project.project_id} value={project.project_id}>
                {project.name}
              </option>
            ))}
          </select>
          <span className="context-divider">/</span>
          <span className="context-label">Revision</span>
          <select
            aria-label="Topology revision"
            value={selectedRevisionId}
            onChange={(event) => setSelectedRevisionId(event.target.value)}
            disabled={!revisions.length}
          >
            {revisions.map((revision) => (
              <option
                key={revision.model_revision_id}
                value={revision.model_revision_id}
              >
                r{revision.revision_number} · {revision.model_revision_label}
              </option>
            ))}
          </select>
        </div>
        <div className="runtime-status">
          <span className={error ? 'status-dot error' : 'status-dot'} />
          {error ? 'API unavailable' : loading ? 'Connecting' : 'Local runtime'}
        </div>
      </header>

      <section className="workspace">
        <aside className="project-rail">
          <div className="rail-heading">
            <span>Workspace</span>
            <button title="Refresh browser data" onClick={() => location.reload()}>
              ↻
            </button>
          </div>
          <nav>
            <button
              className={
                workspaceView === 'topology' ? 'nav-item active' : 'nav-item'
              }
              onClick={() => setWorkspaceView('topology')}
            >
              <span className="nav-icon">⌘</span>
              Topology
            </button>
            <button
              className={
                workspaceView === 'cases' ? 'nav-item active' : 'nav-item'
              }
              onClick={() => setWorkspaceView('cases')}
            >
              <span className="nav-icon">◇</span>
              Cases
            </button>
            <button
              className={
                workspaceView === 'runs' ? 'nav-item active' : 'nav-item'
              }
              onClick={() => setWorkspaceView('runs')}
            >
              <span className="nav-icon">▶</span>
              Runs
            </button>
            <button
              className={
                workspaceView === 'results' ? 'nav-item active' : 'nav-item'
              }
              onClick={() => setWorkspaceView('results')}
            >
              <span className="nav-icon">▥</span>
              Results
            </button>
          </nav>
          <div className="project-summary">
            <span>Current project</span>
            <strong>{selectedProject?.name ?? 'No project'}</strong>
            <p>{selectedProject?.description || 'No description provided.'}</p>
          </div>
          <div className="model-stats">
            <div>
              <strong>{topology?.model.components.length ?? 0}</strong>
              <span>components</span>
            </div>
            <div>
              <strong>{topology?.model.connections.length ?? 0}</strong>
              <span>connections</span>
            </div>
          </div>
        </aside>

        {workspaceView === 'topology' ? (
          <section className="canvas-panel">
          <div className="canvas-toolbar">
            <div>
              <span className="eyebrow">Immutable topology</span>
              <h1>{topology?.model.name ?? 'Thermal system graph'}</h1>
            </div>
            <div className="revision-chip">
              <span>SHA-256</span>
              <code>{selectedRevision?.checksum.slice(7, 19) ?? '—'}</code>
            </div>
          </div>
          {(operationError || operationStatus || publishing) && (
            <div
              className={`operation-banner${operationError ? ' is-error' : ''}`}
            >
              {publishing
                ? 'Publishing immutable child revision…'
                : operationError || operationStatus}
              <button
                type="button"
                onClick={() => {
                  setOperationError('')
                  setOperationStatus('')
                }}
              >
                ×
              </button>
            </div>
          )}
          {error ? (
            <div className="error-state">
              <strong>Could not load the Thermox API</strong>
              <p>{error}</p>
              <code>npm run dev</code>
              <span>expects the API at http://127.0.0.1:8080</span>
            </div>
          ) : (
            <TopologyCanvas
              topology={topology}
              catalog={catalog?.components ?? []}
              revisionId={selectedRevisionId}
              publishing={publishing}
              onConnect={connectPorts}
              onSelect={setSelection}
            />
          )}
          </section>
        ) : workspaceView === 'cases' ? (
          <CaseWorkspace
            key={selectedCaseRevisionId || 'empty-case'}
            revision={selectedCaseRevision}
            publishing={casePublishing}
            operationError={caseOperationError}
            operationStatus={caseOperationStatus}
            artifactRevisions={artifactRevisions}
            requiredArtifactIds={requiredArtifactIds}
            validationResult={validationResult}
            validating={validating}
            onDismissOperation={() => {
              setCaseOperationError('')
              setCaseOperationStatus('')
            }}
            onEdit={editCase}
            onValidate={validateCase}
            onCreate={() => setAddingCase(true)}
          />
        ) : workspaceView === 'runs' ? (
          <RunConfigurationWorkspace
            revision={selectedRunConfiguration}
            publishing={runPublishing}
            operationError={runOperationError}
            operationStatus={runOperationStatus}
            jobs={simulationJobs}
            selectedJobId={selectedSimulationJobId}
            jobsLoading={jobsLoading}
            jobSubmitting={jobSubmitting}
            jobStateFilter={jobStateFilter}
            jobsNextCursor={jobsNextCursor}
            onDismissOperation={() => {
              setRunOperationError('')
              setRunOperationStatus('')
            }}
            onCreate={beginCreateRunConfiguration}
            onRevise={() => setRevisingRunConfiguration(true)}
            onSelectJob={setSelectedSimulationJobId}
            onJobStateFilter={setJobStateFilter}
            onSubmitJob={() => {
              void submitSimulationJob()
            }}
            onRefreshJobs={() => {
              void refreshSimulationJobs()
            }}
            onLoadMoreJobs={() => {
              void loadMoreSimulationJobs()
            }}
            onCancelJob={(job) => {
              void cancelSimulationJob(job)
            }}
          />
        ) : (
          <ResultsWorkspace
            topology={topology}
            catalog={catalog?.components ?? []}
            job={selectedResultJob}
            result={simulationResult}
            loading={resultLoading}
            error={resultError}
            onRetry={() => {
              void retryResult()
            }}
          />
        )}

        <aside className="palette">
          {workspaceView === 'runs' ? (
            <RunConfigurationPanel
              revisions={visibleRunConfigurations}
              selectedId={selectedRunConfigurationRevisionId}
              publishing={runPublishing}
              onSelect={setSelectedRunConfigurationRevisionId}
              onCreate={beginCreateRunConfiguration}
              onRevise={() => setRevisingRunConfiguration(true)}
            />
          ) : workspaceView === 'results' ? (
            <ResultSelectionPanel
              revisions={visibleRunConfigurations}
              selectedRevisionId={selectedRunConfigurationRevisionId}
              jobs={resultJobs}
              selectedJobId={selectedResultJobId}
              loading={resultJobsLoading}
              onSelectRevision={setSelectedRunConfigurationRevisionId}
              onSelectJob={setSelectedResultJobId}
              onRefresh={() => {
                void refreshResultJobs()
              }}
            />
          ) : workspaceView === 'cases' ? (
            <CaseRevisionPanel
              revisions={caseRevisions}
              selectedId={selectedCaseRevisionId}
              publishing={casePublishing}
              onSelect={setSelectedCaseRevisionId}
              onCreate={() => setAddingCase(true)}
            />
          ) : selection && topology && catalog ? (
            <InspectorPanel
              selection={selection}
              topology={topology}
              catalog={catalog}
              publishing={publishing}
              onEditComponent={setEditingComponent}
              onEditConnection={setEditingConnection}
              onRemoveComponent={(component) => {
                void removeComponent(component)
              }}
              onRemoveConnection={(connection) => {
                void removeConnection(connection)
              }}
              onClose={() => setSelection(undefined)}
            />
          ) : (
            <>
              <div className="palette-heading">
                <span className="eyebrow">Runtime catalog</span>
                <div className="palette-title-row">
                  <div>
                    <h2>Components</h2>
                    <p>
                      {catalog?.components.length ?? 0} registered types
                    </p>
                  </div>
                  <button
                    type="button"
                    className="resource-button"
                    disabled={!topology || publishing}
                    onClick={() => setAddingMedium(true)}
                  >
                    + Fluid
                  </button>
                </div>
                <div className="resource-summary">
                  <span>{topology?.model.media.length ?? 0} fluids</span>
                  <span>{artifactRevisions.length} artifact revisions</span>
                </div>
              </div>
              <label className="search">
                <span>⌕</span>
                <input
                  value={filter}
                  onChange={(event) => setFilter(event.target.value)}
                  placeholder="Filter type or domain"
                />
              </label>
              <div className="component-list">
                {palette.map((component) => (
                  <button
                    type="button"
                    className="component-card"
                    key={component.kind}
                    disabled={!topology || publishing}
                    onClick={() => setNewComponentType(component)}
                  >
                    <div>
                      <span className="kind-family">
                        {shortKind(component.kind)}
                      </span>
                      {component.supports_transient && (
                        <span className="transient-badge">transient</span>
                      )}
                    </div>
                    <strong>{component.kind.split('.').at(-1)}</strong>
                    <div className="port-summary">
                      {component.ports.map((port) => (
                        <span key={`${port.name}-${port.domain}`}>
                          <i className={`port-${port.domain}`} />
                          {port.name}
                        </span>
                      ))}
                    </div>
                  </button>
                ))}
              </div>
              <footer>
                <span>Catalog</span>
                <code>
                  {catalog?.fingerprint.slice(0, 18) ?? 'loading…'}
                </code>
              </footer>
            </>
          )}
        </aside>
      </section>
      {workspaceView === 'topology' && newComponentType && topology && (
        <ComponentForm
          key={`new-${newComponentType.kind}`}
          componentType={newComponentType}
          topology={topology}
          artifactRevisions={artifactRevisions}
          onCancel={() => setNewComponentType(undefined)}
          onSubmit={addComponent}
        />
      )}
      {workspaceView === 'topology' &&
        editingComponent &&
        topology &&
        catalog && (
        <ComponentForm
          key={`edit-${editingComponent.id}`}
          componentType={
            catalog.components.find(
              (item) => item.kind === editingComponent.kind,
            )!
          }
          topology={topology}
          artifactRevisions={artifactRevisions}
          component={editingComponent}
          onCancel={() => setEditingComponent(undefined)}
          onSubmit={updateComponent}
        />
      )}
      {workspaceView === 'topology' &&
        editingConnection &&
        topology &&
        catalog && (
        <ConnectionForm
          key={`edit-${editingConnection.id}`}
          connection={editingConnection}
          topology={topology}
          catalog={catalog}
          onCancel={() => setEditingConnection(undefined)}
          onSubmit={(intent) =>
            updateConnection(intent, editingConnection)
          }
        />
      )}
      {workspaceView === 'topology' && addingMedium && topology && catalog && (
        <MediumForm
          backends={catalog.property_backends}
          topology={topology}
          onCancel={() => setAddingMedium(false)}
          onSubmit={addMedium}
        />
      )}
      {addingCase && (
        <CaseCreateForm
          revisions={caseRevisions}
          onCancel={() => setAddingCase(false)}
          onSubmit={createCase}
        />
      )}
      {(addingRunConfiguration || revisingRunConfiguration) &&
        topology &&
        catalog &&
        selectedCaseRevision && (
          <RunConfigurationForm
            key={
              revisingRunConfiguration
                ? `revise-${selectedRunConfigurationRevisionId}`
                : `create-${selectedCaseRevision.case_revision_id}`
            }
            topology={topology}
            catalog={catalog}
            modelRevisionId={selectedRevisionId}
            caseRevision={selectedCaseRevision}
            artifactRevisionIds={selectedArtifactRevisionIds}
            revisions={visibleRunConfigurations}
            base={
              revisingRunConfiguration
                ? selectedRunConfiguration
                : undefined
            }
            onCancel={() => {
              setAddingRunConfiguration(false)
              setRevisingRunConfiguration(false)
            }}
            onSubmit={createRunConfiguration}
          />
        )}
    </main>
  )
}

export default App
