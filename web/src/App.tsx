import { useEffect, useMemo, useState } from 'react'
import type { Connection } from '@xyflow/react'
import { api, errorMessage, isAbortError } from './api'
import { CaseCreateForm } from './CaseCreateForm'
import { CaseRevisionPanel } from './CaseRevisionPanel'
import { CaseWorkspace } from './CaseWorkspace'
import { CalibrationPublishForm } from './CalibrationPublishForm'
import { ComponentForm } from './ComponentForm'
import { ComponentLibrary } from './ComponentLibrary'
import { ConnectionForm } from './ConnectionForm'
import { CorrelationArtifactForm } from './CorrelationArtifactForm'
import {
  DefinitionSidebar,
  DefinitionWorkspace,
} from './DefinitionWorkspace'
import { componentDefinitionReadiness } from './definitionReadiness'
import { useDisplayUnits } from './DisplayUnitsContext'
import { ExpressionComponentForm } from './ExpressionComponentForm'
import {
  buildConnectionOperation,
  type ConnectionIntent,
} from './graphAuthoring'
import {
  InspectorPanel,
  type GraphSelection,
} from './InspectorPanel'
import { MediumForm } from './MediumForm'
import { MaterialForm } from './MaterialForm'
import { PerformanceMapArtifactForm } from './PerformanceMapArtifactForm'
import { RunConfigurationForm } from './RunConfigurationForm'
import { RunConfigurationPanel } from './RunConfigurationPanel'
import { RunConfigurationWorkspace } from './RunConfigurationWorkspace'
import { ResultSelectionPanel } from './ResultSelectionPanel'
import { ResultsWorkspace } from './ResultsWorkspace'
import { StudyPublishForm } from './StudyPublishForm'
import { validationMatchesExecutionSelection } from './runAuthoring'
import {
  mergeProjectComponentCatalog,
  requiredProjectComponentSources,
  resolveTopologyComponentCatalog,
} from './projectComponentCatalog'
import { TopologyCanvas } from './TopologyCanvas'
import { initialTopologyDocument } from './topologyAuthoring'
import { WorkflowNavigator } from './WorkflowNavigator'
import {
  buildWorkflowStages,
  type WorkspaceView,
} from './workflow'
import type {
  ArtifactRevision,
  CalibrationRevision,
  Catalog,
  CatalogComponent,
  CaseDocument,
  CaseEditOperation,
  CaseRevision,
  ComponentDefinition,
  CorrelationArtifactDefinition,
  ConnectionDefinition,
  CreateRunConfiguration,
  CreateCalibrationRevision,
  CreateStudyRevision,
  GraphEditOperation,
  MediumDefinition,
  MaterialDefinition,
  PerformanceMapArtifactDefinition,
  ModelRevision,
  ProjectModelValidation,
  ProjectComponentCatalogEntry,
  Project,
  RunConfigurationRevision,
  ResultProjection,
  SimulationJob,
  SimulationResult,
  SimulationJobState,
  StudyRevision,
  TopologyDocument,
  ValidationDiagnostic,
} from './types'

function App() {
  const {
    profile: displayUnitProfile,
    setProfile: setDisplayUnitProfile,
    setUnitDimensions,
  } = useDisplayUnits()
  const [workspaceView, setWorkspaceView] =
    useState<WorkspaceView>('topology')
  const [catalog, setCatalog] = useState<Catalog>()
  const [projects, setProjects] = useState<Project[]>([])
  const [selectedProjectId, setSelectedProjectId] = useState('')
  const [revisions, setRevisions] = useState<ModelRevision[]>([])
  const [artifactRevisions, setArtifactRevisions] = useState<
    ArtifactRevision[]
  >([])
  const [projectComponents, setProjectComponents] = useState<
    ProjectComponentCatalogEntry[]
  >([])
  const [caseRevisions, setCaseRevisions] = useState<CaseRevision[]>([])
  const [selectedCaseRevisionId, setSelectedCaseRevisionId] = useState('')
  const [selectedCaseRevision, setSelectedCaseRevision] =
    useState<CaseRevision>()
  const [selectedRevisionId, setSelectedRevisionId] = useState('')
  const [topology, setTopology] = useState<TopologyDocument>()
  const [topologySidebar, setTopologySidebar] =
    useState<'library' | 'inspector'>('library')
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
  const [addingMaterial, setAddingMaterial] = useState(false)
  const [addingCorrelation, setAddingCorrelation] = useState(false)
  const [loadingArtifactRevision, setLoadingArtifactRevision] =
    useState(false)
  const [revisingCorrelation, setRevisingCorrelation] = useState<{
    source: ArtifactRevision
    definition: CorrelationArtifactDefinition
  }>()
  const [addingPerformanceMap, setAddingPerformanceMap] = useState(false)
  const [revisingPerformanceMap, setRevisingPerformanceMap] = useState<{
    source: ArtifactRevision
    definition: PerformanceMapArtifactDefinition
  }>()
  const [definingComponent, setDefiningComponent] = useState(false)
  const [revisingComponent, setRevisingComponent] =
    useState<ProjectComponentCatalogEntry>()
  const [addingCase, setAddingCase] = useState(false)
  const [casePublishing, setCasePublishing] = useState(false)
  const [caseOperationError, setCaseOperationError] = useState('')
  const [caseOperationStatus, setCaseOperationStatus] = useState('')
  const [validationResult, setValidationResult] =
    useState<ProjectModelValidation>()
  const [studyRevisions, setStudyRevisions] = useState<StudyRevision[]>([])
  const [addingStudy, setAddingStudy] = useState(false)
  const [calibrationRevisions, setCalibrationRevisions] = useState<
    CalibrationRevision[]
  >([])
  const [addingCalibration, setAddingCalibration] = useState(false)
  const [calibrationJobs, setCalibrationJobs] = useState<SimulationJob[]>([])
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
        setUnitDimensions(catalogResponse.unit_dimensions)
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
    setStudyRevisions([])
    if (!selectedProjectId) return
    const controller = new AbortController()
    api
      .studyRevisions(selectedProjectId, controller.signal)
      .then((response) => {
        setCaseOperationError('')
        setStudyRevisions(
          [...response.study_revisions].sort(
            (left, right) =>
              right.created_at_epoch_ms - left.created_at_epoch_ms,
          ),
        )
      })
      .catch((reason: unknown) => {
        if (!isAbortError(reason)) setCaseOperationError(errorMessage(reason))
      })
    return () => controller.abort()
  }, [selectedProjectId])

  useEffect(() => {
    setCalibrationRevisions([])
    setCalibrationJobs([])
    if (!selectedProjectId) return
    const controller = new AbortController()
    Promise.all([
      api.calibrationRevisions(selectedProjectId, controller.signal),
      api.projectJobs(selectedProjectId, controller.signal),
    ])
      .then(([response, jobs]) => {
        setCalibrationRevisions(
          [...response.calibration_revisions].sort(
            (left, right) =>
              right.created_at_epoch_ms - left.created_at_epoch_ms,
          ),
        )
        setCalibrationJobs(
          jobs.jobs.filter((job) => job.request.mode === 'calibration'),
        )
      })
      .catch((reason: unknown) => {
        if (!isAbortError(reason)) setCaseOperationError(errorMessage(reason))
      })
    return () => controller.abort()
  }, [selectedProjectId])

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
    setProjectComponents([])
    setAddingCorrelation(false)
    setRevisingCorrelation(undefined)
    setAddingPerformanceMap(false)
    setRevisingPerformanceMap(undefined)
    setSelectedRevisionId('')
    setTopology(undefined)
    if (!selectedProjectId) return
    const controller = new AbortController()
    Promise.all([
      api.modelRevisions(selectedProjectId, controller.signal),
      api.artifactRevisions(selectedProjectId, controller.signal),
      api.projectComponentCatalog(selectedProjectId, controller.signal),
    ])
      .then(([response, artifacts, components]) => {
        setError('')
        const ordered = [...response.model_revisions].sort(
          (left, right) => right.revision_number - left.revision_number,
        )
        setRevisions(ordered)
        setArtifactRevisions(artifacts.artifact_revisions)
        setProjectComponents(components.components)
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
  const effectiveCatalog = useMemo<Catalog | undefined>(() => {
    if (!catalog) return undefined
    return mergeProjectComponentCatalog(
      catalog,
      projectComponents,
    )
  }, [catalog, projectComponents])
  const topologyCatalog = useMemo<Catalog | undefined>(() => {
    if (!catalog) return undefined
    return resolveTopologyComponentCatalog(
      catalog,
      projectComponents,
      topology,
    )
  }, [catalog, projectComponents, topology])
  const physicalReadiness = useMemo(
    () => componentDefinitionReadiness(topology, topologyCatalog),
    [topology, topologyCatalog],
  )
  const physicalIssueCount = useMemo(
    () =>
      Object.values(physicalReadiness).reduce(
        (total, item) => total + item.issues.length,
        0,
      ),
    [physicalReadiness],
  )
  const requiredComponentSources = useMemo(
    () =>
      requiredProjectComponentSources(
        topology,
        projectComponents,
      ),
    [projectComponents, topology],
  )
  const preferredArtifactRevisionIds = useMemo(
    () =>
      Object.fromEntries(
        requiredComponentSources.map((entry) => [
          entry.source.artifact_id,
          entry.source.artifact_revision_id,
        ]),
      ),
    [requiredComponentSources],
  )
  const requiredArtifactIds = useMemo(
    () =>
      [
        ...new Set(
          [
            ...(topology?.model.components ?? []).flatMap(
              (component) =>
                Object.values(component.artifacts ?? {}),
            ),
            ...requiredComponentSources.map(
              (entry) => entry.source.artifact_id,
            ),
          ],
        ),
      ].sort(),
    [requiredComponentSources, topology],
  )
  const visibleRunConfigurations = useMemo(
    () => {
      const studyIds = new Set(
        studyRevisions
          .filter(
            (revision) =>
              revision.model_revision_id === selectedRevisionId,
          )
          .map((revision) => revision.study_revision_id),
      )
      return runConfigurationRevisions.filter((revision) =>
        studyIds.has(revision.study_revision_id),
      )
    },
    [runConfigurationRevisions, selectedRevisionId, studyRevisions],
  )
  const visibleStudies = useMemo(
    () =>
      studyRevisions.filter(
        (revision) => revision.model_revision_id === selectedRevisionId,
      ),
    [selectedRevisionId, studyRevisions],
  )
  const visibleCalibrations = useMemo(
    () =>
      calibrationRevisions.filter(
        (revision) => revision.model_revision_id === selectedRevisionId,
      ),
    [calibrationRevisions, selectedRevisionId],
  )
  const selectedRunStudy = useMemo(
    () =>
      studyRevisions.find(
        (study) =>
          study.study_revision_id ===
          selectedRunConfiguration?.study_revision_id,
      ),
    [selectedRunConfiguration, studyRevisions],
  )
  const selectedArtifactRevisionIds = useMemo(
    () =>
      requiredArtifactIds
        .map((artifactId) => {
          const validated = validationResult?.artifact_revisions.find(
            (revision) => revision.artifact_id === artifactId,
          )
          if (validated) return validated.artifact_revision_id
          const preferred =
            preferredArtifactRevisionIds[artifactId]
          if (preferred) return preferred
          return artifactRevisions
            .filter((revision) => revision.artifact_id === artifactId)
            .sort(
              (left, right) =>
                right.revision_number - left.revision_number,
            )[0]?.artifact_revision_id
        })
        .filter((id): id is string => Boolean(id)),
    [
      artifactRevisions,
      preferredArtifactRevisionIds,
      requiredArtifactIds,
      validationResult,
    ],
  )
  const activePublishedStudy = useMemo(
    () =>
      visibleStudies.find(
        (study) =>
          study.case_revision_id === selectedCaseRevisionId &&
          study.artifact_revision_ids.length ===
            selectedArtifactRevisionIds.length &&
          study.artifact_revision_ids.every((id) =>
            selectedArtifactRevisionIds.includes(id),
          ),
      ),
    [
      selectedArtifactRevisionIds,
      selectedCaseRevisionId,
      visibleStudies,
    ],
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
  const calibrationActiveJobCount = calibrationJobs.filter(
    (job) => job.state === 'queued' || job.state === 'running',
  ).length
  const succeededJobCount = simulationJobs.filter(
    (job) => job.state === 'succeeded',
  ).length
  const exactRevisionCompiled = Boolean(
    selectedCaseRevision &&
      validationMatchesExecutionSelection(
        validationResult,
        selectedRevisionId,
        selectedCaseRevision.case_revision_id,
        selectedArtifactRevisionIds,
      ),
  )
  const unresolvedArtifactCount = Math.max(
    0,
    requiredArtifactIds.length - selectedArtifactRevisionIds.length,
  )
  const workflowStages = buildWorkflowStages({
    componentCount: topology?.model.components.length ?? 0,
    connectionCount: topology?.model.connections.length ?? 0,
    mediumCount: topology?.model.media.length ?? 0,
    definitionIssueCount: physicalIssueCount,
    hasCase: Boolean(selectedCaseRevision),
    unresolvedArtifactCount,
    compiled: exactRevisionCompiled,
    studyRevisionCount: visibleStudies.length,
    variableCount:
      validationResult?.validation.compilation.variable_count ?? 0,
    equationCount:
      validationResult?.validation.compilation.equation_count ?? 0,
    runConfigurationCount: visibleRunConfigurations.length,
    activeJobCount,
    succeededJobCount,
  })

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
    if (
      workspaceView !== 'studies' ||
      !selectedProjectId ||
      calibrationActiveJobCount === 0
    ) return
    const timer = window.setInterval(() => {
      void api.projectJobs(selectedProjectId)
        .then((page) => {
          setCalibrationJobs(
            page.jobs.filter((job) => job.request.mode === 'calibration'),
          )
        })
        .catch((reason: unknown) => {
          setCaseOperationError(errorMessage(reason))
        })
    }, 4000)
    return () => window.clearInterval(timer)
  }, [calibrationActiveJobCount, selectedProjectId, workspaceView])

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

  async function createInitialTopology() {
    const project = projects.find(
      (candidate) => candidate.project_id === selectedProjectId,
    )
    if (!project || selectedRevisionId) return
    setPublishing(true)
    setOperationError('')
    setOperationStatus('')
    try {
      const revision = await api.createModelRevision(
        project.project_id,
        initialTopologyDocument(project),
      )
      setRevisions((current) => [
        revision,
        ...current.filter(
          (item) => item.model_revision_id !== revision.model_revision_id,
        ),
      ])
      setSelectedRevisionId(revision.model_revision_id)
      setOperationStatus(
        `Created ${project.name} topology revision r${revision.revision_number}.`,
      )
    } catch (reason) {
      setOperationError(errorMessage(reason))
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

  async function addMaterial(material: MaterialDefinition) {
    await publishEdits(
      [
        {
          action: 'upsert',
          entity_type: 'material',
          entity_id: material.id,
          entity: { ...material },
        },
      ],
      `Added reacting mixture ${material.id}.`,
    )
    setAddingMaterial(false)
  }

  async function publishExpressionComponent(
    artifactId: string,
    parentArtifactRevisionId: string,
    definition: ProjectComponentCatalogEntry['definition'],
  ) {
    if (!selectedProjectId) {
      throw new Error('Select a project before defining a component.')
    }
    setPublishing(true)
    setOperationError('')
    setOperationStatus('')
    try {
      const revision = await api.createExpressionComponentRevision(
        selectedProjectId,
        artifactId,
        parentArtifactRevisionId,
        definition,
      )
      const [artifacts, components] = await Promise.all([
        api.artifactRevisions(selectedProjectId),
        api.projectComponentCatalog(selectedProjectId),
      ])
      setArtifactRevisions(artifacts.artifact_revisions)
      setProjectComponents(components.components)
      setDefiningComponent(false)
      setRevisingComponent(undefined)
      setOperationStatus(
        `Published ${definition.kind} ${definition.version} as ${artifactId} r${revision.revision_number}.`,
      )
    } catch (reason) {
      const message = errorMessage(reason)
      setOperationError(message)
      throw new Error(message)
    } finally {
      setPublishing(false)
    }
  }

  async function publishCorrelation(
    artifactId: string,
    parentArtifactRevisionId: string,
    definition: CorrelationArtifactDefinition,
  ) {
    if (!selectedProjectId) {
      throw new Error('Select a project before publishing engineering data.')
    }
    setPublishing(true)
    setOperationError('')
    setOperationStatus('')
    try {
      const revision = await api.createCorrelationRevision(
        selectedProjectId,
        artifactId,
        parentArtifactRevisionId,
        definition,
      )
      const artifacts = await api.artifactRevisions(selectedProjectId)
      setArtifactRevisions(artifacts.artifact_revisions)
      setAddingCorrelation(false)
      setRevisingCorrelation(undefined)
      setOperationStatus(
        `Published correlation ${artifactId} r${revision.revision_number}.`,
      )
    } catch (reason) {
      const message = errorMessage(reason)
      setOperationError(message)
      throw new Error(message)
    } finally {
      setPublishing(false)
    }
  }

  async function reviseCorrelation(source: ArtifactRevision) {
    if (!selectedProjectId) return
    setLoadingArtifactRevision(true)
    setOperationError('')
    setOperationStatus('')
    try {
      const content = await api.artifactRevision<CorrelationArtifactDefinition>(
        selectedProjectId,
        source.artifact_revision_id,
      )
      if (
        content.revision.artifact_type !== 'thermox.correlation' ||
        content.artifact.schema_version !== 'thermox.correlation/v1'
      ) {
        throw new Error('The selected revision is not a supported correlation artifact.')
      }
      setAddingCorrelation(false)
      setAddingPerformanceMap(false)
      setRevisingPerformanceMap(undefined)
      setRevisingCorrelation({
        source: content.revision,
        definition: content.artifact,
      })
    } catch (reason) {
      setOperationError(errorMessage(reason))
    } finally {
      setLoadingArtifactRevision(false)
    }
  }

  async function publishPerformanceMap(
    artifactId: string,
    parentArtifactRevisionId: string,
    definition: PerformanceMapArtifactDefinition,
  ) {
    if (!selectedProjectId) {
      throw new Error('Select a project before publishing engineering data.')
    }
    setPublishing(true)
    setOperationError('')
    setOperationStatus('')
    try {
      const revision = await api.createPerformanceMapRevision(
        selectedProjectId,
        artifactId,
        parentArtifactRevisionId,
        definition,
      )
      const artifacts = await api.artifactRevisions(selectedProjectId)
      setArtifactRevisions(artifacts.artifact_revisions)
      setAddingPerformanceMap(false)
      setRevisingPerformanceMap(undefined)
      setOperationStatus(
        `Published performance map ${artifactId} r${revision.revision_number}.`,
      )
    } catch (reason) {
      const message = errorMessage(reason)
      setOperationError(message)
      throw new Error(message)
    } finally {
      setPublishing(false)
    }
  }

  async function revisePerformanceMap(source: ArtifactRevision) {
    if (!selectedProjectId) return
    setLoadingArtifactRevision(true)
    setOperationError('')
    setOperationStatus('')
    try {
      const content = await api.artifactRevision<PerformanceMapArtifactDefinition>(
        selectedProjectId,
        source.artifact_revision_id,
      )
      if (
        content.revision.artifact_type !== 'thermox.performance_map' ||
        content.revision.artifact_schema_version !== 'thermox.performance_map/v1'
      ) {
        throw new Error('The selected revision is not an ordinary v1 performance map.')
      }
      setAddingPerformanceMap(false)
      setAddingCorrelation(false)
      setRevisingCorrelation(undefined)
      setRevisingPerformanceMap({ source: content.revision, definition: content.artifact })
    } catch (reason) {
      setOperationError(errorMessage(reason))
    } finally {
      setLoadingArtifactRevision(false)
    }
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

  async function publishStudy(resultProjections: ResultProjection[]) {
    if (
      !selectedProjectId ||
      !selectedRevisionId ||
      !selectedCaseRevision ||
      !exactRevisionCompiled
    ) {
      setCaseOperationError(
        'Validate the exact topology, case, and artifact revisions before publishing a study.',
      )
      return
    }
    const studyId = selectedCaseRevision.case_id
    const parent = visibleStudies.find(
      (revision) => revision.study_id === studyId,
    )
    const request: CreateStudyRevision = {
      schema_version: 'thermox.study_revision.create/v1',
      study_id: studyId,
      parent_study_revision_id: parent?.study_revision_id ?? '',
      model_revision_id: selectedRevisionId,
      case_revision_id: selectedCaseRevision.case_revision_id,
      intent: selectedCaseRevision.mode,
      artifact_revision_ids: selectedArtifactRevisionIds,
      result_projections: resultProjections,
    }
    setCasePublishing(true)
    setCaseOperationError('')
    setCaseOperationStatus('')
    try {
      const revision = await api.createStudyRevision(
        selectedProjectId,
        request,
      )
      setStudyRevisions((current) => [
        revision,
        ...current.filter(
          (item) =>
            item.study_revision_id !== revision.study_revision_id,
        ),
      ])
      setCaseOperationStatus(
        `Published study ${revision.study_id} r${revision.revision_number}.`,
      )
      setAddingStudy(false)
    } catch (reason) {
      setCaseOperationError(errorMessage(reason))
    } finally {
      setCasePublishing(false)
    }
  }

  async function publishCalibration(request: CreateCalibrationRevision) {
    if (!selectedProjectId || !selectedRevisionId) {
      throw new Error('Select an exact topology revision first.')
    }
    setCasePublishing(true)
    setCaseOperationError('')
    setCaseOperationStatus('')
    try {
      const revision = await api.createCalibrationRevision(
        selectedProjectId, request,
      )
      setCalibrationRevisions((current) => [revision, ...current])
      setAddingCalibration(false)
      setCaseOperationStatus(
        `Published calibration ${revision.calibration_id} r${revision.revision_number}.`,
      )
    } catch (reason) {
      const message = errorMessage(reason)
      setCaseOperationError(message)
      throw new Error(message)
    } finally {
      setCasePublishing(false)
    }
  }

  async function runCalibration(revision: CalibrationRevision) {
    if (!selectedProjectId) return
    setCaseOperationError('')
    setCaseOperationStatus('Submitting calibration…')
    try {
      const job = await api.submitCalibration(
        selectedProjectId,
        revision.calibration_revision_id,
        crypto.randomUUID(),
      )
      setCalibrationJobs((current) => [
        job,
        ...current.filter((item) => item.job_id !== job.job_id),
      ])
      setCaseOperationStatus(`Calibration job ${job.job_id} is ${job.state}.`)
    } catch (reason) {
      setCaseOperationError(errorMessage(reason))
      setCaseOperationStatus('')
    }
  }

  function beginCreateRunConfiguration() {
    if (!activePublishedStudy) {
      setRunOperationError(
        'Publish the validated topology, case, artifacts, and outputs as a Study before configuring execution.',
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
    if (!topology || !topologyCatalog) {
      throw new Error('Topology and runtime catalog are required.')
    }
    const operation = buildConnectionOperation(
      connection,
      topology,
      topologyCatalog,
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
    if (!topologyCatalog) {
      setOperationError('The runtime catalog is not loaded.')
      return
    }
    try {
      const operation = buildConnectionOperation(
        connection,
        topology,
        topologyCatalog,
      )
      await publishEdits(
        [operation],
        `Connected ${connection.source}.${connection.sourceHandle} to ${connection.target}.${connection.targetHandle}.`,
      )
    } catch (reason) {
      setOperationError(errorMessage(reason))
    }
  }

  function inspectComponentDefinition(componentId: string) {
    const component = topology?.model.components.find(
      (item) => item.id === componentId,
    )
    if (!component) {
      setCaseOperationError(
        `Component ${componentId} is not present in this topology revision.`,
      )
      return
    }
    setEditingComponent(component)
    setWorkspaceView('definition')
  }

  function inspectDiagnosticOnCanvas(diagnostic: ValidationDiagnostic) {
    if (diagnostic.component_id) {
      inspectComponentDefinition(diagnostic.component_id)
      return
    }
    if (
      diagnostic.connection_id &&
      topology?.model.connections.some(
        (item) => item.id === diagnostic.connection_id,
      )
    ) {
      setSelection({ type: 'connection', id: diagnostic.connection_id })
      setTopologySidebar('inspector')
      setWorkspaceView('topology')
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
        <div className="runtime-tools">
          <label className="unit-profile">
            <span>Units</span>
            <select
              aria-label="Display unit profile"
              value={displayUnitProfile}
              onChange={(event) =>
                setDisplayUnitProfile(
                  event.target.value as 'si' | 'engineering',
                )
              }
            >
              <option value="si">SI</option>
              <option value="engineering">Engineering</option>
            </select>
          </label>
          <div className="runtime-status">
            <span className={error ? 'status-dot error' : 'status-dot'} />
            {error
              ? 'API unavailable'
              : loading
                ? 'Connecting'
                : 'Local runtime'}
          </div>
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
          <WorkflowNavigator
            currentView={workspaceView}
            stages={workflowStages}
            calculatable={exactRevisionCompiled}
            onSelect={setWorkspaceView}
          />
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
              catalog={topologyCatalog?.components ?? []}
              revisionId={selectedRevisionId}
              publishing={publishing}
              componentReadiness={physicalReadiness}
              onCreateTopology={
                selectedProjectId && !selectedRevisionId
                  ? () => {
                      void createInitialTopology()
                    }
                  : undefined
              }
              onConnect={connectPorts}
              onSelect={(nextSelection) => {
                setSelection(nextSelection)
                setTopologySidebar(
                  nextSelection ? 'inspector' : 'library',
                )
              }}
              onAddComponent={(component) => {
                setSelection(undefined)
                setTopologySidebar('library')
                setNewComponentType(component)
              }}
            />
          )}
          </section>
        ) : workspaceView === 'definition' ? (
          <DefinitionWorkspace
            topology={topology}
            catalog={topologyCatalog}
            readiness={physicalReadiness}
            artifactRevisions={artifactRevisions}
            publishing={publishing}
            loadingArtifactRevision={loadingArtifactRevision}
            operationError={operationError}
            operationStatus={operationStatus}
            onDismissOperation={() => {
              setOperationError('')
              setOperationStatus('')
            }}
            onEditComponent={setEditingComponent}
            onAddFluid={() => setAddingMedium(true)}
            onAddMaterial={() => setAddingMaterial(true)}
            onAddCorrelation={() => {
              setAddingPerformanceMap(false)
              setRevisingPerformanceMap(undefined)
              setRevisingCorrelation(undefined)
              setAddingCorrelation(true)
            }}
            onReviseCorrelation={(revision) => {
              void reviseCorrelation(revision)
            }}
            onAddPerformanceMap={() => {
              setAddingCorrelation(false)
              setRevisingCorrelation(undefined)
              setRevisingPerformanceMap(undefined)
              setAddingPerformanceMap(true)
            }}
            onRevisePerformanceMap={(revision) => {
              void revisePerformanceMap(revision)
            }}
            onBuild={() => setWorkspaceView('topology')}
          />
        ) : workspaceView === 'studies' ? (
          <CaseWorkspace
            key={selectedCaseRevisionId || 'empty-case'}
            revision={selectedCaseRevision}
            publishing={casePublishing}
            operationError={caseOperationError}
            operationStatus={caseOperationStatus}
            artifactRevisions={artifactRevisions}
            requiredArtifactIds={requiredArtifactIds}
            preferredArtifactRevisionIds={
              preferredArtifactRevisionIds
            }
            topology={topology}
            catalog={topologyCatalog}
            unresolvedArtifactCount={unresolvedArtifactCount}
            exactRevisionCompiled={exactRevisionCompiled}
            validationResult={validationResult}
            validating={validating}
            onDismissOperation={() => {
              setCaseOperationError('')
              setCaseOperationStatus('')
            }}
            onEdit={editCase}
            onValidate={validateCase}
            onInspectComponent={inspectComponentDefinition}
            onInspectDiagnostic={inspectDiagnosticOnCanvas}
            onCreate={() => setAddingCase(true)}
          />
        ) : workspaceView === 'runs' ? (
          <RunConfigurationWorkspace
            revision={selectedRunConfiguration}
            study={selectedRunStudy}
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
            topologyRevisionId={selectedRevisionId}
            catalog={topologyCatalog?.components ?? []}
            job={selectedResultJob}
            result={simulationResult}
            loading={resultLoading}
            error={resultError}
            onRetry={() => {
              void retryResult()
            }}
          />
        )}

        <aside
          className={`palette${
            workspaceView === 'topology' ? ' topology-sidebar' : ''
          }`}
        >
          {workspaceView === 'topology' ? (
            <>
              <div className="topology-sidebar-tabs">
                <button
                  type="button"
                  className={topologySidebar === 'library' ? 'active' : ''}
                  onClick={() => setTopologySidebar('library')}
                >
                  Library
                  <span>
                    {effectiveCatalog?.components.length ?? 0}
                  </span>
                </button>
                <button
                  type="button"
                  className={topologySidebar === 'inspector' ? 'active' : ''}
                  disabled={!selection}
                  onClick={() => setTopologySidebar('inspector')}
                >
                  Inspector
                </button>
              </div>
              <div className="topology-sidebar-content">
                {topologySidebar === 'inspector' &&
                selection &&
                topology &&
                topologyCatalog ? (
                  <InspectorPanel
                    selection={selection}
                    topology={topology}
                    catalog={topologyCatalog}
                    publishing={publishing}
                    onEditComponent={setEditingComponent}
                    onEditConnection={setEditingConnection}
                    onRemoveComponent={(component) => {
                      void removeComponent(component)
                    }}
                    onRemoveConnection={(connection) => {
                      void removeConnection(connection)
                    }}
                    onClose={() => {
                      setSelection(undefined)
                      setTopologySidebar('library')
                    }}
                  />
                ) : (
                  <ComponentLibrary
                    components={
                      effectiveCatalog?.components ?? []
                    }
                    disabled={!topology || publishing}
                    fluidCount={topology?.model.media.length ?? 0}
                    materialCount={topology?.model.materials?.length ?? 0}
                    artifactRevisionCount={artifactRevisions.length}
                    catalogFingerprint={
                      effectiveCatalog?.fingerprint ?? ''
                    }
                    onChoose={setNewComponentType}
                    onAddFluid={() => setAddingMedium(true)}
                    onAddMaterial={() => setAddingMaterial(true)}
                    onAddCorrelation={() => setAddingCorrelation(true)}
                    onCreateTopology={
                      selectedProjectId && !selectedRevisionId
                        ? () => {
                            void createInitialTopology()
                          }
                        : undefined
                    }
                    onDefine={() => setDefiningComponent(true)}
                    onRevise={(component) => {
                      const sourceRevisionId =
                        component.source_artifact_revision_id
                      const entry = projectComponents.find(
                        (candidate) =>
                          candidate.source.artifact_revision_id ===
                          sourceRevisionId,
                      )
                      if (entry) setRevisingComponent(entry)
                    }}
                  />
                )}
              </div>
            </>
          ) : workspaceView === 'definition' ? (
            <DefinitionSidebar
              topology={topology}
              readiness={physicalReadiness}
              onSelectComponent={setEditingComponent}
            />
          ) : workspaceView === 'runs' ? (
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
          ) : workspaceView === 'studies' ? (
            <CaseRevisionPanel
              revisions={caseRevisions}
              studies={visibleStudies}
              calibrations={visibleCalibrations}
              calibrationJobs={calibrationJobs}
              selectedId={selectedCaseRevisionId}
              publishing={casePublishing}
              canPublishStudy={exactRevisionCompiled}
              onSelect={setSelectedCaseRevisionId}
              onCreate={() => setAddingCase(true)}
              onPublishStudy={() => {
                setAddingStudy(true)
              }}
              onPublishCalibration={() => setAddingCalibration(true)}
              onRunCalibration={(revision) => {
                void runCalibration(revision)
              }}
            />
          ) : null}
        </aside>
      </section>
      {workspaceView === 'topology' && newComponentType && topology && (
        <ComponentForm
          key={`new-${newComponentType.kind}`}
          componentType={newComponentType}
          intent="draft"
          topology={topology}
          artifactRevisions={artifactRevisions}
          onCancel={() => setNewComponentType(undefined)}
          onSubmit={addComponent}
        />
      )}
      {(workspaceView === 'topology' || workspaceView === 'definition') &&
        editingComponent &&
        topology &&
        topologyCatalog && (
        <ComponentForm
          key={`edit-${editingComponent.id}`}
          componentType={
            topologyCatalog.components.find(
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
        topologyCatalog && (
        <ConnectionForm
          key={`edit-${editingConnection.id}`}
          connection={editingConnection}
          topology={topology}
          catalog={topologyCatalog}
          onCancel={() => setEditingConnection(undefined)}
          onSubmit={(intent) =>
            updateConnection(intent, editingConnection)
          }
        />
      )}
      {(workspaceView === 'topology' || workspaceView === 'definition') &&
        addingMedium && topology && catalog && (
        <MediumForm
          backends={catalog.property_backends}
          topology={topology}
          onCancel={() => setAddingMedium(false)}
          onSubmit={addMedium}
        />
      )}
      {(workspaceView === 'topology' || workspaceView === 'definition') &&
        addingMaterial && topology && catalog && (
        <MaterialForm
          backends={catalog.thermochemistry_backends}
          topology={topology}
          onCancel={() => setAddingMaterial(false)}
          onSubmit={addMaterial}
        />
      )}
      {(workspaceView === 'topology' || workspaceView === 'definition') &&
        (addingCorrelation || revisingCorrelation) && catalog && (
          <CorrelationArtifactForm
            key={
              revisingCorrelation
                ? `revise-correlation-${revisingCorrelation.source.artifact_revision_id}`
                : 'new-correlation'
            }
            unitDimensions={catalog.unit_dimensions}
            artifactRevisions={artifactRevisions}
            base={revisingCorrelation}
            onCancel={() => {
              setAddingCorrelation(false)
              setRevisingCorrelation(undefined)
            }}
            onSubmit={publishCorrelation}
          />
        )}
      {(workspaceView === 'topology' || workspaceView === 'definition') &&
        (addingPerformanceMap || revisingPerformanceMap) && catalog && (
          <PerformanceMapArtifactForm
            key={
              revisingPerformanceMap
                ? `revise-map-${revisingPerformanceMap.source.artifact_revision_id}`
                : 'new-performance-map'
            }
            unitDimensions={catalog.unit_dimensions}
            artifactRevisions={artifactRevisions}
            base={revisingPerformanceMap}
            onCancel={() => {
              setAddingPerformanceMap(false)
              setRevisingPerformanceMap(undefined)
            }}
            onSubmit={publishPerformanceMap}
          />
        )}
      {workspaceView === 'topology' &&
        (definingComponent || revisingComponent) &&
        catalog && (
          <ExpressionComponentForm
            key={
              revisingComponent
                ? `revise-component-${revisingComponent.source.artifact_revision_id}`
                : 'define-component'
            }
            connectorDomains={catalog.connector_domains}
            unitDimensions={catalog.unit_dimensions}
            base={revisingComponent}
            onCancel={() => {
              setDefiningComponent(false)
              setRevisingComponent(undefined)
            }}
            onSubmit={publishExpressionComponent}
          />
        )}
      {addingCase && (
        <CaseCreateForm
          revisions={caseRevisions}
          onCancel={() => setAddingCase(false)}
          onSubmit={createCase}
        />
      )}
      {addingStudy &&
        topology &&
        topologyCatalog &&
        selectedCaseRevision && (
          <StudyPublishForm
            topology={topology}
            catalog={topologyCatalog}
            base={activePublishedStudy}
            transient={
              selectedCaseRevision.mode.includes('dynamic') ||
              selectedCaseRevision.mode.includes('transient')
            }
            onCancel={() => setAddingStudy(false)}
            onSubmit={publishStudy}
          />
        )}
      {addingCalibration && selectedRevisionId && (
        <CalibrationPublishForm
          modelRevisionId={selectedRevisionId}
          studies={visibleStudies}
          cases={caseRevisions}
          onCancel={() => setAddingCalibration(false)}
          onSubmit={publishCalibration}
        />
      )}
      {(addingRunConfiguration || revisingRunConfiguration) &&
        (revisingRunConfiguration ? selectedRunStudy : activePublishedStudy) && (
          <RunConfigurationForm
            key={
              revisingRunConfiguration
                ? `revise-${selectedRunConfigurationRevisionId}`
                : `create-${activePublishedStudy?.study_revision_id}`
            }
            study={
              (revisingRunConfiguration
                ? selectedRunStudy
                : activePublishedStudy)!
            }
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
