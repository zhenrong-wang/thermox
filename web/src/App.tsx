import { lazy, Suspense, useEffect, useMemo, useRef, useState } from 'react'
import type { Connection } from '@xyflow/react'
import { api, errorMessage, isAbortError } from './api'
import { AssemblyForm } from './AssemblyForm'
import { AssemblyTemplateForm } from './AssemblyTemplateForm'
import { AssemblyTemplateInstanceForm } from './AssemblyTemplateInstanceForm'
import { BalanceUncertaintyArtifactForm } from './BalanceUncertaintyArtifactForm'
import { CaseCreateForm } from './CaseCreateForm'
import { CaseRevisionPanel } from './CaseRevisionPanel'
import { CalibrationPublishForm } from './CalibrationPublishForm'
import { ReconciliationPublishForm } from './ReconciliationPublishForm'
import { ReconciliationResultPanel } from './ReconciliationResultPanel'
import { ComponentForm } from './ComponentForm'
import { ComponentLibrary } from './ComponentLibrary'
import { ConnectionForm } from './ConnectionForm'
import { CorrelationArtifactForm } from './CorrelationArtifactForm'
import {
  DefinitionSidebar,
  DefinitionWorkspace,
} from './DefinitionWorkspace'
import {
  componentDefinitionReadiness,
  definitionIssues,
} from './definitionReadiness'
import { useDisplayUnits } from './DisplayUnitsContext'
import { ExpressionComponentForm } from './ExpressionComponentForm'
import {
  buildAssemblyGroupingOperations,
  buildAssemblyTemplateInstantiationOperations,
  buildAssemblyUngroupingOperations,
} from './assemblyAuthoring'
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
import { SystemReadinessPanel } from './SystemReadinessPanel'
import { ResultSelectionPanel } from './ResultSelectionPanel'
import { StudyPublishForm } from './StudyPublishForm'
import { StudyPackageWorkbench } from './StudyPackageWorkbench'
import {
  packageRunConfiguration,
  packageStudyRevision,
  type StudyPackageDocument,
} from './studyPackage'
import { studyArtifactRevisionIds } from './studyAuthoring'
import { ValidationSeriesArtifactForm } from './ValidationSeriesArtifactForm'
import { ValidationCampaignForm } from './ValidationCampaignForm'
import {
  validationMatchesExecutionSelection,
  validationTargetsExecutionSelection,
} from './runAuthoring'
import {
  mergeProjectComponentCatalog,
  requiredProjectComponentSources,
  resolveTopologyComponentCatalog,
} from './projectComponentCatalog'
import { initialTopologyDocument } from './topologyAuthoring'
import { TopologyJsonWorkbench } from './TopologyJsonWorkbench'
import {
  buildSystemReadiness,
  type SystemReadinessIssue,
} from './systemReadiness'
import {
  resolveStudyArtifactSelections,
  selectedStudyArtifactRevisionIds,
  studyMatchesPreparationSelection,
} from './studyPreparation'
import { WorkflowNavigator } from './WorkflowNavigator'
import {
  withPlacedEntity,
  type CanvasComponentPlacement,
} from './topologyPresentation'
import {
  buildWorkflowStages,
  type WorkspaceView,
} from './workflow'
import type {
  ArtifactRevision,
  BalanceUncertaintyModel,
  AssemblyTemplateCatalogEntry,
  AssemblyDefinition,
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
  CreateReconciliationRevision,
  CreateStudyRevision,
  EngineeringAcceptanceCriterion,
  GraphEditOperation,
  JobValidationReport,
  MediumDefinition,
  MaterialDefinition,
  PerformanceMapArtifactDefinition,
  ModelRevision,
  ProjectModelValidation,
  ProjectComponentCatalogEntry,
  Project,
  ReconciliationResult,
  ReconciliationRevision,
  RunConfigurationRevision,
  ResultProjection,
  SimulationJob,
  SimulationJobComparison,
  SimulationResult,
  SimulationJobState,
  StudyRevision,
  StudyTrajectoryValidationBinding,
  TopologyDocument,
  TopologyPresentation,
  ValidationDiagnostic,
  ValidationCampaignArtifact,
  ValidationCampaignCatalogEntry,
  ValidationSeriesArtifact,
  ValidationSeriesCatalogEntry,
} from './types'

const CaseWorkspace = lazy(() =>
  import('./CaseWorkspace').then((module) => ({
    default: module.CaseWorkspace,
  })),
)
const ResultsWorkspace = lazy(() =>
  import('./ResultsWorkspace').then((module) => ({
    default: module.ResultsWorkspace,
  })),
)
const RunConfigurationWorkspace = lazy(() =>
  import('./RunConfigurationWorkspace').then((module) => ({
    default: module.RunConfigurationWorkspace,
  })),
)
const TopologyCanvas = lazy(() =>
  import('./TopologyCanvas').then((module) => ({
    default: module.TopologyCanvas,
  })),
)

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
  const [studyArtifactSelections, setStudyArtifactSelections] = useState<
    Record<string, string>
  >({})
  const [projectComponents, setProjectComponents] = useState<
    ProjectComponentCatalogEntry[]
  >([])
  const [caseRevisions, setCaseRevisions] = useState<CaseRevision[]>([])
  const [selectedCaseRevisionId, setSelectedCaseRevisionId] = useState('')
  const [selectedCaseRevision, setSelectedCaseRevision] =
    useState<CaseRevision>()
  const [selectedRevisionId, setSelectedRevisionId] = useState('')
  const [topology, setTopology] = useState<TopologyDocument>()
  const [showingTopologyJson, setShowingTopologyJson] = useState(false)
  const [topologyPresentation, setTopologyPresentation] =
    useState<TopologyPresentation>()
  const [layoutSaveState, setLayoutSaveState] =
    useState<'idle' | 'saving' | 'saved' | 'error'>('idle')
  const layoutSaveTimer = useRef<number | undefined>(undefined)
  const [error, setError] = useState('')
  const [loading, setLoading] = useState(true)
  const [publishing, setPublishing] = useState(false)
  const [operationError, setOperationError] = useState('')
  const [operationStatus, setOperationStatus] = useState('')
  const [newComponentType, setNewComponentType] =
    useState<CatalogComponent>()
  const [newComponentPlacement, setNewComponentPlacement] =
    useState<CanvasComponentPlacement>()
  const [editingComponent, setEditingComponent] =
    useState<ComponentDefinition>()
  const [editingConnection, setEditingConnection] =
    useState<ConnectionDefinition>()
  const [selection, setSelection] = useState<GraphSelection>()
  const [showSystemReadiness, setShowSystemReadiness] = useState(false)
  const [addingMedium, setAddingMedium] = useState(false)
  const [addingMaterial, setAddingMaterial] = useState(false)
  const [addingAssembly, setAddingAssembly] = useState(false)
  const [assemblyTemplates, setAssemblyTemplates] = useState<
    AssemblyTemplateCatalogEntry[]
  >([])
  const [publishingAssemblyTemplate, setPublishingAssemblyTemplate] =
    useState<AssemblyDefinition>()
  const [instantiatingAssemblyTemplate, setInstantiatingAssemblyTemplate] =
    useState<AssemblyTemplateCatalogEntry>()
  const [addingCorrelation, setAddingCorrelation] = useState(false)
  const [loadingArtifactRevision, setLoadingArtifactRevision] =
    useState(false)
  const [revisingCorrelation, setRevisingCorrelation] = useState<{
    source: ArtifactRevision
    definition: CorrelationArtifactDefinition
  }>()
  const [addingPerformanceMap, setAddingPerformanceMap] = useState(false)
  const [addingBalanceUncertainty, setAddingBalanceUncertainty] = useState(false)
  const [revisingBalanceUncertainty, setRevisingBalanceUncertainty] = useState<{
    source: ArtifactRevision
    definition: BalanceUncertaintyModel
  }>()
  const [addingValidationSeries, setAddingValidationSeries] = useState(false)
  const [validationSeries, setValidationSeries] = useState<
    ValidationSeriesCatalogEntry[]
  >([])
  const [validationCampaigns, setValidationCampaigns] = useState<
    ValidationCampaignCatalogEntry[]
  >([])
  const [addingValidationCampaign, setAddingValidationCampaign] =
    useState(false)
  const [revisingValidationCampaign, setRevisingValidationCampaign] =
    useState<ValidationCampaignCatalogEntry>()
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
  const [showingStudyPackage, setShowingStudyPackage] = useState(false)
  const [studyPackageInitial, setStudyPackageInitial] =
    useState<StudyPackageDocument>()
  const [studyPackagePublishing, setStudyPackagePublishing] = useState(false)
  const [calibrationRevisions, setCalibrationRevisions] = useState<
    CalibrationRevision[]
  >([])
  const [addingCalibration, setAddingCalibration] = useState(false)
  const [calibrationJobs, setCalibrationJobs] = useState<SimulationJob[]>([])
  const [reconciliationRevisions, setReconciliationRevisions] = useState<
    ReconciliationRevision[]
  >([])
  const [addingReconciliation, setAddingReconciliation] = useState(false)
  const [reconciliationJobs, setReconciliationJobs] = useState<SimulationJob[]>([])
  const [reconciliationResult, setReconciliationResult] =
    useState<ReconciliationResult>()
  const [reconciliationResultLoading, setReconciliationResultLoading] =
    useState(false)
  const [reconciliationResultError, setReconciliationResultError] =
    useState('')
  const [showingReconciliationResult, setShowingReconciliationResult] =
    useState(false)
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
  const [comparisonJobs, setComparisonJobs] = useState<SimulationJob[]>([])
  const [resultComparison, setResultComparison] =
    useState<SimulationJobComparison>()
  const [comparisonLoading, setComparisonLoading] = useState(false)
  const [comparisonError, setComparisonError] = useState('')
  const [validationReport, setValidationReport] =
    useState<JobValidationReport>()
  const [validationReportLoading, setValidationReportLoading] = useState(false)
  const [validationReportError, setValidationReportError] = useState('')

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
    setAssemblyTemplates([])
    if (!selectedProjectId) return
    const latest = new Map<string, ArtifactRevision>()
    for (const revision of artifactRevisions) {
      if (revision.artifact_type !== 'thermox.assembly_template') continue
      const current = latest.get(revision.artifact_id)
      if (!current || revision.revision_number > current.revision_number) {
        latest.set(revision.artifact_id, revision)
      }
    }
    if (latest.size === 0) return
    const controller = new AbortController()
    Promise.all(
      [...latest.values()].map(async (source) => {
        const content = await api.artifactRevision<TopologyDocument>(
          selectedProjectId,
          source.artifact_revision_id,
          controller.signal,
        )
        return { source, definition: content.artifact }
      }),
    )
      .then((templates) =>
        setAssemblyTemplates(
          templates.sort((left, right) =>
            left.source.artifact_id.localeCompare(right.source.artifact_id),
          ),
        ),
      )
      .catch((reason: unknown) => {
        if (!isAbortError(reason)) setOperationError(errorMessage(reason))
      })
    return () => controller.abort()
  }, [artifactRevisions, selectedProjectId])

  useEffect(() => {
    setValidationSeries([])
    if (!selectedProjectId) return
    const sources = artifactRevisions.filter(
      (revision) => revision.artifact_type === 'thermox.validation_series',
    )
    if (!sources.length) return
    const controller = new AbortController()
    Promise.all(sources.map(async (source) => {
      const content = await api.artifactRevision<ValidationSeriesArtifact>(
        selectedProjectId,
        source.artifact_revision_id,
        controller.signal,
      )
      return { source, definition: content.artifact }
    }))
      .then(setValidationSeries)
      .catch((reason: unknown) => {
        if (!isAbortError(reason)) setOperationError(errorMessage(reason))
      })
    return () => controller.abort()
  }, [artifactRevisions, selectedProjectId])

  useEffect(() => {
    setValidationCampaigns([])
    if (!selectedProjectId) return
    const sources = artifactRevisions.filter(
      (revision) => revision.artifact_type === 'thermox.validation_campaign',
    )
    if (!sources.length) return
    const controller = new AbortController()
    Promise.all(sources.map(async (source) => {
      const content = await api.artifactRevision<ValidationCampaignArtifact>(
        selectedProjectId,
        source.artifact_revision_id,
        controller.signal,
      )
      return { source, definition: content.artifact }
    }))
      .then((campaigns) => setValidationCampaigns(campaigns.sort(
        (left, right) =>
          right.source.created_at_epoch_ms - left.source.created_at_epoch_ms,
      )))
      .catch((reason: unknown) => {
        if (!isAbortError(reason)) setOperationError(errorMessage(reason))
      })
    return () => controller.abort()
  }, [artifactRevisions, selectedProjectId])

  useEffect(() => {
    setCalibrationRevisions([])
    setCalibrationJobs([])
    setReconciliationRevisions([])
    setReconciliationJobs([])
    if (!selectedProjectId) return
    const controller = new AbortController()
    Promise.all([
      api.calibrationRevisions(selectedProjectId, controller.signal),
      api.reconciliationRevisions(selectedProjectId, controller.signal),
      api.projectJobs(selectedProjectId, controller.signal),
    ])
      .then(([response, reconciliations, jobs]) => {
        setCalibrationRevisions(
          [...response.calibration_revisions].sort(
            (left, right) =>
              right.created_at_epoch_ms - left.created_at_epoch_ms,
          ),
        )
        setCalibrationJobs(
          jobs.jobs.filter((job) => job.request.mode === 'calibration'),
        )
        setReconciliationRevisions(
          [...reconciliations.reconciliation_revisions].sort(
            (left, right) =>
              right.created_at_epoch_ms - left.created_at_epoch_ms,
          ),
        )
        setReconciliationJobs(
          jobs.jobs.filter((job) => job.request.mode === 'reconciliation'),
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
    setStudyArtifactSelections({})
    setProjectComponents([])
    setAddingCorrelation(false)
    setRevisingCorrelation(undefined)
    setAddingPerformanceMap(false)
    setRevisingPerformanceMap(undefined)
    setSelectedRevisionId('')
    setNewComponentType(undefined)
    setNewComponentPlacement(undefined)
    setShowSystemReadiness(false)
    setTopology(undefined)
    setShowingTopologyJson(false)
    setShowingStudyPackage(false)
    setStudyPackageInitial(undefined)
    setTopologyPresentation(undefined)
    setLayoutSaveState('idle')
    if (layoutSaveTimer.current !== undefined) {
      window.clearTimeout(layoutSaveTimer.current)
      layoutSaveTimer.current = undefined
    }
    if (!selectedProjectId) return
    const controller = new AbortController()
    Promise.all([
      api.modelRevisions(selectedProjectId, controller.signal),
      api.artifactRevisions(selectedProjectId, controller.signal),
      api.projectComponentCatalog(selectedProjectId, controller.signal),
      api.topologyPresentation(selectedProjectId, controller.signal),
    ])
      .then(([response, artifacts, components, presentation]) => {
        setError('')
        const ordered = [...response.model_revisions].sort(
          (left, right) => right.revision_number - left.revision_number,
        )
        setRevisions(ordered)
        setArtifactRevisions(artifacts.artifact_revisions)
        setProjectComponents(components.components)
        setTopologyPresentation(presentation?.presentation)
        setLayoutSaveState(presentation ? 'saved' : 'idle')
        setSelectedRevisionId(ordered[0]?.model_revision_id ?? '')
      })
      .catch((reason: unknown) => {
        if (!isAbortError(reason)) setError(errorMessage(reason))
      })
    return () => controller.abort()
  }, [selectedProjectId])

  useEffect(
    () => () => {
      if (layoutSaveTimer.current !== undefined) {
        window.clearTimeout(layoutSaveTimer.current)
      }
    },
    [],
  )

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
    () => definitionIssues(topology, topologyCatalog).length,
    [topology, topologyCatalog],
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
  useEffect(() => {
    setStudyArtifactSelections((current) =>
      resolveStudyArtifactSelections(
        requiredArtifactIds,
        artifactRevisions,
        preferredArtifactRevisionIds,
        current,
      ),
    )
  }, [
    artifactRevisions,
    preferredArtifactRevisionIds,
    requiredArtifactIds,
  ])
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
  const visibleReconciliations = useMemo(
    () =>
      reconciliationRevisions.filter(
        (revision) => revision.model_revision_id === selectedRevisionId,
      ),
    [reconciliationRevisions, selectedRevisionId],
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
    () => selectedStudyArtifactRevisionIds(
      requiredArtifactIds,
      studyArtifactSelections,
    ),
    [requiredArtifactIds, studyArtifactSelections],
  )
  const activePublishedStudy = useMemo(
    () =>
      visibleStudies.find(
        (study) =>
          studyMatchesPreparationSelection(
            study,
            selectedCaseRevisionId,
            selectedArtifactRevisionIds,
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
  const reconciliationActiveJobCount = reconciliationJobs.filter(
    (job) => job.state === 'queued' || job.state === 'running',
  ).length
  const succeededJobCount = simulationJobs.filter(
    (job) => job.state === 'succeeded',
  ).length
  const exactRevisionValidation =
    selectedCaseRevision &&
    validationTargetsExecutionSelection(
      validationResult,
      selectedRevisionId,
      selectedCaseRevision.case_revision_id,
      selectedArtifactRevisionIds,
    )
      ? validationResult
      : undefined
  const exactRevisionCompiled = Boolean(
    selectedCaseRevision &&
      validationMatchesExecutionSelection(
        exactRevisionValidation,
        selectedRevisionId,
        selectedCaseRevision.case_revision_id,
        selectedArtifactRevisionIds,
      ),
  )
  const unresolvedArtifactCount = Math.max(
    0,
    requiredArtifactIds.length - selectedArtifactRevisionIds.length,
  )
  const systemReadiness = useMemo(
    () =>
      buildSystemReadiness(
        topology,
        topologyCatalog,
        Boolean(selectedCaseRevision),
        unresolvedArtifactCount,
        exactRevisionValidation,
      ),
    [
      exactRevisionValidation,
      selectedCaseRevision,
      topology,
      topologyCatalog,
      unresolvedArtifactCount,
    ],
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
      calibrationActiveJobCount + reconciliationActiveJobCount === 0
    ) return
    const timer = window.setInterval(() => {
      void api.projectJobs(selectedProjectId)
        .then((page) => {
          setCalibrationJobs(
            page.jobs.filter((job) => job.request.mode === 'calibration'),
          )
          setReconciliationJobs(
            page.jobs.filter((job) => job.request.mode === 'reconciliation'),
          )
        })
        .catch((reason: unknown) => {
          setCaseOperationError(errorMessage(reason))
        })
    }, 4000)
    return () => window.clearInterval(timer)
  }, [
    calibrationActiveJobCount,
    reconciliationActiveJobCount,
    selectedProjectId,
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

  useEffect(() => {
    setResultComparison(undefined)
    setComparisonError('')
    setValidationReport(undefined)
    setValidationReportError('')
    setComparisonJobs([])
    if (workspaceView !== 'results' || !selectedProjectId) return
    const controller = new AbortController()
    api.projectJobs(selectedProjectId, controller.signal)
      .then((page) => setComparisonJobs(page.jobs.filter((job) =>
        ['succeeded', 'failed', 'cancelled'].includes(job.state) &&
        ['steady', 'transient'].includes(job.request.mode) &&
        Boolean(job.request.source_revisions?.study_revision_id),
      )))
      .catch((reason: unknown) => {
        if (!isAbortError(reason)) setComparisonError(errorMessage(reason))
      })
    return () => controller.abort()
  }, [selectedProjectId, selectedResultJobId, workspaceView])

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

  async function publishTopologyDocument(document: TopologyDocument) {
    if (!selectedProjectId) {
      throw new Error('Select a project before publishing topology JSON.')
    }
    setPublishing(true)
    setOperationError('')
    setOperationStatus('')
    try {
      const revision = await api.createModelRevision(
        selectedProjectId,
        document,
        selectedRevisionId,
      )
      setRevisions((current) => [
        revision,
        ...current.filter(
          (item) => item.model_revision_id !== revision.model_revision_id,
        ),
      ])
      setSelection(undefined)
      setShowSystemReadiness(false)
      setTopologyPresentation(undefined)
      setLayoutSaveState('idle')
      setSelectedRevisionId(revision.model_revision_id)
      setOperationStatus(
        `Published topology JSON as immutable revision r${revision.revision_number}.`,
      )
    } catch (reason) {
      const message = errorMessage(reason)
      setOperationError(message)
      throw new Error(message)
    } finally {
      setPublishing(false)
    }
  }

  async function addComponent(component: ComponentDefinition) {
    const child = await publishEdits(
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
    if (newComponentPlacement && selectedProjectId) {
      const nextPresentation = withPlacedEntity(
        newComponentPlacement,
        component.id,
      )
      setTopologyPresentation(nextPresentation)
      setLayoutSaveState('saving')
      try {
        await api.putTopologyPresentation(
          selectedProjectId,
          child.model_revision_id,
          nextPresentation,
        )
        setLayoutSaveState('saved')
      } catch (reason) {
        setLayoutSaveState('error')
        setOperationError(
          `The component was added, but its canvas position could not be saved: ${errorMessage(reason)}`,
        )
      }
    }
    setNewComponentType(undefined)
    setNewComponentPlacement(undefined)
  }

  function beginComponentDraft(
    component: CatalogComponent,
    placement?: CanvasComponentPlacement,
  ) {
    setNewComponentType(component)
    setNewComponentPlacement(placement)
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
        content.artifact.schema_version !== 'thermox.correlation/v2'
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

  async function publishBalanceUncertainty(
    artifactId: string,
    parentArtifactRevisionId: string,
    definition: BalanceUncertaintyModel,
  ) {
    if (!selectedProjectId) {
      throw new Error('Select a project before publishing metrology data.')
    }
    setPublishing(true)
    setOperationError('')
    setOperationStatus('')
    try {
      const revision = await api.createBalanceUncertaintyRevision(
        selectedProjectId,
        artifactId,
        parentArtifactRevisionId,
        definition,
      )
      const artifacts = await api.artifactRevisions(selectedProjectId)
      setArtifactRevisions(artifacts.artifact_revisions)
      setAddingBalanceUncertainty(false)
      setRevisingBalanceUncertainty(undefined)
      setOperationStatus(
        `Published boundary metrology ${artifactId} r${revision.revision_number}.`,
      )
    } catch (reason) {
      const message = errorMessage(reason)
      setOperationError(message)
      throw new Error(message)
    } finally {
      setPublishing(false)
    }
  }

  async function reviseBalanceUncertainty(source: ArtifactRevision) {
    if (!selectedProjectId) return
    setLoadingArtifactRevision(true)
    setOperationError('')
    setOperationStatus('')
    try {
      const content = await api.artifactRevision<BalanceUncertaintyModel>(
        selectedProjectId,
        source.artifact_revision_id,
      )
      if (
        content.revision.artifact_type !== 'thermox.balance_uncertainty' ||
        content.artifact.schema_version !== 'thermox.balance_uncertainty/v1'
      ) {
        throw new Error('The selected revision is not supported boundary metrology.')
      }
      setAddingBalanceUncertainty(false)
      setRevisingBalanceUncertainty({
        source: content.revision,
        definition: content.artifact,
      })
    } catch (reason) {
      setOperationError(errorMessage(reason))
    } finally {
      setLoadingArtifactRevision(false)
    }
  }

  async function publishValidationSeries(
    artifactId: string,
    parentArtifactRevisionId: string,
    definition: ValidationSeriesArtifact,
  ) {
    if (!selectedProjectId) {
      throw new Error('Select a project before publishing validation data.')
    }
    setPublishing(true)
    setOperationError('')
    setOperationStatus('')
    try {
      const revision = await api.createValidationSeriesRevision(
        selectedProjectId,
        artifactId,
        parentArtifactRevisionId,
        definition,
      )
      const artifacts = await api.artifactRevisions(selectedProjectId)
      setArtifactRevisions(artifacts.artifact_revisions)
      setAddingValidationSeries(false)
      setOperationStatus(
        `Published validation data ${artifactId} r${revision.revision_number}.`,
      )
    } catch (reason) {
      const message = errorMessage(reason)
      setOperationError(message)
      throw new Error(message)
    } finally {
      setPublishing(false)
    }
  }

  async function publishValidationCampaign(
    artifactId: string,
    parentArtifactRevisionId: string,
    definition: ValidationCampaignArtifact,
  ) {
    if (!selectedProjectId) {
      throw new Error('Select a project before publishing a campaign.')
    }
    setPublishing(true)
    setOperationError('')
    setOperationStatus('')
    try {
      const revision = await api.createValidationCampaignRevision(
        selectedProjectId,
        artifactId,
        parentArtifactRevisionId,
        definition,
      )
      const artifacts = await api.artifactRevisions(selectedProjectId)
      setArtifactRevisions(artifacts.artifact_revisions)
      setAddingValidationCampaign(false)
      setRevisingValidationCampaign(undefined)
      setOperationStatus(
        `Published validation campaign ${artifactId} r${revision.revision_number}.`,
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

  async function publishStudy(
    resultProjections: ResultProjection[],
    acceptanceCriteria: EngineeringAcceptanceCriterion[],
    trajectoryValidationBindings: StudyTrajectoryValidationBinding[],
    balanceUncertaintyArtifactRevisionId: string,
  ) {
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
    const artifactRevisionIds = studyArtifactRevisionIds(
      selectedArtifactRevisionIds,
      trajectoryValidationBindings,
      balanceUncertaintyArtifactRevisionId
        ? [balanceUncertaintyArtifactRevisionId] : [],
    )
    const request: CreateStudyRevision = {
      schema_version: 'thermox.study_revision.create/v5',
      study_id: studyId,
      parent_study_revision_id: parent?.study_revision_id ?? '',
      model_revision_id: selectedRevisionId,
      case_revision_id: selectedCaseRevision.case_revision_id,
      intent: selectedCaseRevision.mode,
      artifact_revision_ids: artifactRevisionIds,
      artifact_qualification_requirements: [],
      artifact_operating_envelopes: [],
      result_projections: resultProjections,
      acceptance_criteria: acceptanceCriteria,
      trajectory_validation_bindings: trajectoryValidationBindings,
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

  function openStudyPackageWorkbench() {
    setCaseOperationError('')
    setCaseOperationStatus('')
    const study = activePublishedStudy
    const caseDocument = selectedCaseRevision?.case_document
    if (!topology || !caseDocument || !study) {
      setStudyPackageInitial(undefined)
      setShowingStudyPackage(true)
      return
    }
    const missingRevisionId = study.artifact_revision_ids.find(
      (revisionId) => !artifactRevisions.some(
        (candidate) => candidate.artifact_revision_id === revisionId,
      ),
    )
    if (missingRevisionId) {
      setCaseOperationError(
        `Study artifact revision ${missingRevisionId} is not loaded in this project.`,
      )
      return
    }
    const dependencies = study.artifact_revision_ids.map((revisionId) => {
      const revision = artifactRevisions.find(
        (candidate) => candidate.artifact_revision_id === revisionId,
      )!
      return {
        artifact_revision_id: revision.artifact_revision_id,
        checksum: revision.content.checksum,
        artifact_id: revision.artifact_id,
        artifact_type: revision.artifact_type,
        artifact_schema_version: revision.artifact_schema_version,
      }
    })
    const runConfiguration = runConfigurationRevisions.find(
      (revision) => revision.study_revision_id === study.study_revision_id,
    )
    setStudyPackageInitial({
      schema_version: 'thermox.study_package/v1',
      package_id: `${study.study_id}-r${study.revision_number}`,
      topology,
      case: caseDocument,
      artifact_dependencies: dependencies,
      study: packageStudyRevision(study),
      ...(runConfiguration
        ? { run_configuration: packageRunConfiguration(runConfiguration) }
        : {}),
    })
    setShowingStudyPackage(true)
  }

  async function importStudyPackage(document: StudyPackageDocument) {
    if (!selectedProjectId) {
      throw new Error('Select a project before importing a Study package.')
    }
    setStudyPackagePublishing(true)
    setCaseOperationError('')
    setCaseOperationStatus('Verifying and importing Study package…')
    try {
      const imported = await api.importStudyPackage(
        selectedProjectId,
        document,
        selectedRevisionId,
      )
      setRevisions((current) => [
        imported.model_revision,
        ...current.filter((item) =>
          item.model_revision_id !== imported.model_revision.model_revision_id),
      ])
      setSelectedRevisionId(imported.model_revision.model_revision_id)
      setCaseRevisions([imported.case_revision])
      setSelectedCaseRevision(imported.case_revision)
      setSelectedCaseRevisionId(imported.case_revision.case_revision_id)
      setValidationResult(imported.validation)
      setStudyRevisions((current) => [
        imported.study_revision,
        ...current.filter((item) =>
          item.study_revision_id !== imported.study_revision.study_revision_id),
      ])
      if (imported.run_configuration_revision) {
        const runRevision = imported.run_configuration_revision
        setRunConfigurationRevisions((current) => [
          runRevision,
          ...current.filter((item) =>
            item.run_configuration_revision_id !==
            runRevision.run_configuration_revision_id),
        ])
        setSelectedRunConfiguration(runRevision)
        setSelectedRunConfigurationRevisionId(
          runRevision.run_configuration_revision_id,
        )
      }
      setCaseOperationStatus(
        `Imported Study package ${document.package_id} as immutable, validated revisions.`,
      )
    } catch (reason) {
      const message = errorMessage(reason)
      setCaseOperationError(message)
      setCaseOperationStatus('')
      throw new Error(message)
    } finally {
      setStudyPackagePublishing(false)
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

  async function publishReconciliation(
    request: CreateReconciliationRevision,
  ) {
    if (!selectedProjectId || !selectedRevisionId) {
      throw new Error('Select an exact topology revision first.')
    }
    setCasePublishing(true)
    setCaseOperationError('')
    setCaseOperationStatus('')
    try {
      const revision = await api.createReconciliationRevision(
        selectedProjectId,
        request,
      )
      setReconciliationRevisions((current) => [revision, ...current])
      setAddingReconciliation(false)
      setCaseOperationStatus(
        `Published reconciliation ${revision.reconciliation_id} r${revision.revision_number}.`,
      )
    } catch (reason) {
      const message = errorMessage(reason)
      setCaseOperationError(message)
      throw new Error(message)
    } finally {
      setCasePublishing(false)
    }
  }

  async function runReconciliation(revision: ReconciliationRevision) {
    if (!selectedProjectId) return
    setCaseOperationError('')
    setCaseOperationStatus('Submitting data reconciliation…')
    try {
      const job = await api.submitReconciliation(
        selectedProjectId,
        revision.reconciliation_revision_id,
        crypto.randomUUID(),
      )
      setReconciliationJobs((current) => [
        job,
        ...current.filter((item) => item.job_id !== job.job_id),
      ])
      setCaseOperationStatus(
        `Reconciliation job ${job.job_id} is ${job.state}.`,
      )
    } catch (reason) {
      setCaseOperationError(errorMessage(reason))
      setCaseOperationStatus('')
    }
  }

  async function inspectReconciliationResult(job: SimulationJob) {
    setShowingReconciliationResult(true)
    setReconciliationResult(undefined)
    setReconciliationResultError('')
    setReconciliationResultLoading(true)
    try {
      setReconciliationResult(await api.reconciliationResult(job.job_id))
    } catch (reason) {
      setReconciliationResultError(errorMessage(reason))
    } finally {
      setReconciliationResultLoading(false)
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
    if (
      !selectedProjectId ||
      !selectedRunConfigurationRevisionId ||
      !selectedRunConfiguration ||
      !selectedRunStudy ||
      selectedRunConfiguration.study_revision_id !==
        selectedRunStudy.study_revision_id
    ) {
      setRunOperationError(
        'Execution is blocked because the selected configuration does not resolve to its exact published Study revision.',
      )
      return
    }
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

  async function compareResultJob(candidateJobId: string) {
    if (!selectedResultJobId) return
    setComparisonLoading(true)
    setComparisonError('')
    try {
      setResultComparison(await api.compareSimulationJobs(
        selectedResultJobId,
        candidateJobId,
      ))
    } catch (reason) {
      setResultComparison(undefined)
      setComparisonError(errorMessage(reason))
    } finally {
      setComparisonLoading(false)
    }
  }

  async function generateValidationReport(
    campaignArtifactRevisionId: string,
    jobIds: string[],
  ) {
    if (!selectedProjectId) return
    setValidationReportLoading(true)
    setValidationReportError('')
    try {
      setValidationReport(await api.validationReport(
        selectedProjectId,
        campaignArtifactRevisionId,
        jobIds,
      ))
    } catch (reason) {
      setValidationReport(undefined)
      setValidationReportError(errorMessage(reason))
    } finally {
      setValidationReportLoading(false)
    }
  }

  async function exportValidationReport(
    campaignArtifactRevisionId: string,
    jobIds: string[],
    format: 'markdown' | 'csv',
  ) {
    if (!selectedProjectId) {
      throw new Error('Select a project before exporting a report.')
    }
    return api.validationReportExport(
      selectedProjectId,
      campaignArtifactRevisionId,
      jobIds,
      format,
    )
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
      setSelection(undefined)
    } catch {
      // publishEdits exposes the service diagnostic in the workspace.
    }
  }

  async function removeAssembly(assembly: AssemblyDefinition) {
    if (
      !window.confirm(
        `Remove ${assembly.id}? Its complete hierarchy and attached connections will be removed in the new revision.`,
      )
    ) {
      return
    }
    try {
      await publishEdits(
        [{
          action: 'remove',
          entity_type: 'assembly',
          entity_id: assembly.id,
          cascade: true,
        }],
        `Removed ${assembly.id}.`,
      )
      setSelection(undefined)
    } catch {
      // publishEdits exposes the service diagnostic in the workspace.
    }
  }

  async function groupComponents(
    assemblyId: string,
    label: string,
    componentIds: string[],
    parameterExports: NonNullable<AssemblyDefinition['parameters']>,
  ) {
    if (!topology) throw new Error('Select a topology revision first.')
    const operations = buildAssemblyGroupingOperations(
      topology,
      assemblyId,
      label,
      componentIds,
      parameterExports,
    )
    await publishEdits(
      operations,
      `Grouped ${componentIds.length} component${componentIds.length === 1 ? '' : 's'} into ${assemblyId}.`,
    )
    setAddingAssembly(false)
  }

  async function ungroupAssembly(assembly: AssemblyDefinition) {
    if (!topology) return
    if (
      !window.confirm(
        `Ungroup ${assembly.id}? Its direct children and internal connections will return to the top-level canvas. Cases using assembly public parameters may need a new revision.`,
      )
    ) {
      return
    }
    try {
      await publishEdits(
        buildAssemblyUngroupingOperations(topology, assembly.id),
        `Ungrouped ${assembly.id} for direct editing.`,
      )
      setSelection(undefined)
    } catch (reason) {
      setOperationError(errorMessage(reason))
    }
  }

  async function publishAssemblyTemplate(
    artifactId: string,
    parentArtifactRevisionId: string,
    definition: TopologyDocument,
  ) {
    if (!selectedProjectId) throw new Error('Select a project first.')
    const revision = await api.createAssemblyTemplateRevision(
      selectedProjectId,
      artifactId,
      parentArtifactRevisionId,
      definition,
    )
    const artifacts = await api.artifactRevisions(selectedProjectId)
    setArtifactRevisions(artifacts.artifact_revisions)
    setPublishingAssemblyTemplate(undefined)
    setOperationStatus(
      `Published assembly template ${artifactId} revision ${revision.revision_number}.`,
    )
  }

  async function instantiateAssemblyTemplate(
    instanceId: string,
    label: string,
  ) {
    if (!topology || !instantiatingAssemblyTemplate) {
      throw new Error('Select a topology and assembly template first.')
    }
    await publishEdits(
      buildAssemblyTemplateInstantiationOperations(
        topology,
        instantiatingAssemblyTemplate.definition,
        instanceId,
        label,
      ),
      `Instantiated ${instantiatingAssemblyTemplate.source.artifact_id} as ${instanceId}.`,
    )
    setInstantiatingAssemblyTemplate(undefined)
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
      setSelection(undefined)
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

  function deleteGraphSelection(candidate: GraphSelection) {
    if (!topology) return
    if (candidate.type === 'component') {
      const component = topology.model.components.find(
        (item) => item.id === candidate.id,
      )
      if (component) void removeComponent(component)
      return
    }
    if (candidate.type === 'assembly') {
      const assembly = (topology.model.assemblies ?? []).find(
        (item) => item.id === candidate.id,
      )
      if (assembly) void removeAssembly(assembly)
      return
    }
    const connection = topology.model.connections.find(
      (item) => item.id === candidate.id,
    )
    if (connection) void removeConnection(connection)
  }

  function updateTopologyPresentation(next: TopologyPresentation) {
    setTopologyPresentation(next)
    if (!selectedProjectId || !selectedRevisionId) return
    setLayoutSaveState('saving')
    if (layoutSaveTimer.current !== undefined) {
      window.clearTimeout(layoutSaveTimer.current)
    }
    const projectId = selectedProjectId
    const revisionId = selectedRevisionId
    layoutSaveTimer.current = window.setTimeout(() => {
      layoutSaveTimer.current = undefined
      void api
        .putTopologyPresentation(projectId, revisionId, next)
        .then(() => setLayoutSaveState('saved'))
        .catch((reason: unknown) => {
          setLayoutSaveState('error')
          setOperationError(
            `Canvas layout could not be saved: ${errorMessage(reason)}`,
          )
        })
    }, 450)
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
      setWorkspaceView('topology')
      return
    }
    setWorkspaceView('studies')
  }

  function inspectSystemReadinessIssue(issue: SystemReadinessIssue) {
    setShowSystemReadiness(false)
    const { target } = issue
    if (target.type === 'component') {
      inspectComponentDefinition(target.id)
      return
    }
    if (target.type === 'assembly' || target.type === 'connection') {
      setSelection({ type: target.type, id: target.id })
      setWorkspaceView('topology')
      return
    }
    if (target.type === 'compiler') {
      inspectDiagnosticOnCanvas(target.diagnostic)
      return
    }
    setSelection(undefined)
    setWorkspaceView(target.view)
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

        <Suspense
          fallback={(
            <section className="workspace-loading" aria-live="polite">
              Loading workspace…
            </section>
          )}
        >
        {workspaceView === 'topology' ? (
          <section className="canvas-panel">
          <div className="canvas-toolbar">
            <div>
              <span className="eyebrow">Immutable topology</span>
              <h1>{topology?.model.name ?? 'Thermal system graph'}</h1>
            </div>
            <div className="canvas-toolbar-actions">
              <span
                className={`layout-save-state ${layoutSaveState}`}
                aria-live="polite"
              >
                {layoutSaveState === 'saving'
                  ? 'Saving layout…'
                  : layoutSaveState === 'saved'
                    ? 'Layout saved'
                    : layoutSaveState === 'error'
                      ? 'Layout save failed'
                      : 'Layout not saved'}
              </span>
              <button
                type="button"
                onClick={() => setShowingTopologyJson(true)}
              >
                JSON
              </button>
              <button
                type="button"
                className={`model-authoring-status${
                  exactRevisionCompiled ? ' is-ready' : ''
                }`}
                onClick={() => {
                  setSelection(undefined)
                  setShowSystemReadiness(true)
                }}
              >
                {exactRevisionCompiled
                  ? 'Calculatable'
                  : `${systemReadiness.issues.length} readiness issues`}
              </button>
              <button type="button" onClick={() => setWorkspaceView('definition')}>
                Define
              </button>
              <button type="button" onClick={() => setWorkspaceView('studies')}>
                Check
              </button>
              <button
                type="button"
                className="primary"
                disabled={!exactRevisionCompiled}
                onClick={() => setWorkspaceView('runs')}
              >
                Calculate
              </button>
              <div className="revision-chip">
                <span>SHA-256</span>
                <code>{selectedRevision?.checksum.slice(7, 19) ?? '—'}</code>
              </div>
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
            <div className="topology-stage">
              <div className="topology-canvas-surface">
                <TopologyCanvas
                  topology={topology}
                  catalog={topologyCatalog?.components ?? []}
                  revisionId={selectedRevisionId}
                  publishing={publishing}
                  componentReadiness={physicalReadiness}
                  selection={selection}
                  presentation={topologyPresentation}
                  onPresentationChange={updateTopologyPresentation}
                  onCreateTopology={
                    selectedProjectId && !selectedRevisionId
                      ? () => {
                          void createInitialTopology()
                        }
                      : undefined
                  }
                  onConnect={connectPorts}
                  onSelect={(nextSelection) => {
                    setShowSystemReadiness(false)
                    setSelection(nextSelection)
                  }}
                  onAddComponent={(component, placement) => {
                    setSelection(undefined)
                    beginComponentDraft(component, placement)
                  }}
                  onDeleteSelection={deleteGraphSelection}
                />
              </div>
              <aside
                className={`topology-inspector-dock ${
                  selection || showSystemReadiness
                    ? 'has-selection'
                    : 'is-empty'
                }`}
              >
                {showSystemReadiness ? (
                  <SystemReadinessPanel
                    readiness={systemReadiness}
                    onInspect={inspectSystemReadinessIssue}
                    onValidate={() => {
                      setShowSystemReadiness(false)
                      setWorkspaceView('studies')
                    }}
                    onClose={() => setShowSystemReadiness(false)}
                  />
                ) : selection && topology && topologyCatalog ? (
                  <InspectorPanel
                    selection={selection}
                    topology={topology}
                    catalog={topologyCatalog}
                    publishing={publishing}
                    definition={
                      selection.type === 'component'
                        ? physicalReadiness[selection.id]
                        : undefined
                    }
                    onEditComponent={setEditingComponent}
                    onEditConnection={setEditingConnection}
                    onRemoveComponent={(component) => {
                      void removeComponent(component)
                    }}
                    onRemoveAssembly={(assembly) => {
                      void removeAssembly(assembly)
                    }}
                    onUngroupAssembly={(assembly) => {
                      void ungroupAssembly(assembly)
                    }}
                    onPublishAssemblyTemplate={setPublishingAssemblyTemplate}
                    onRemoveConnection={(connection) => {
                      void removeConnection(connection)
                    }}
                    onClose={() => setSelection(undefined)}
                  />
                ) : (
                  <div className="inspector-empty-state">
                    <span className="eyebrow">Instance inspector</span>
                    <strong>Select an item on the canvas</strong>
                    <p>
                      Inspect and edit the selected component, assembly, or connection without hiding the equipment library.
                    </p>
                  </div>
                )}
              </aside>
            </div>
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
            onDefineComponent={() => setDefiningComponent(true)}
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
            onAddBalanceUncertainty={() => {
              setRevisingBalanceUncertainty(undefined)
              setAddingBalanceUncertainty(true)
            }}
            onReviseBalanceUncertainty={(revision) => {
              void reviseBalanceUncertainty(revision)
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
            artifactSelections={studyArtifactSelections}
            onArtifactSelectionChange={(artifactId, revisionId) =>
              setStudyArtifactSelections((current) => ({
                ...current,
                [artifactId]: revisionId,
              }))
            }
            topology={topology}
            catalog={topologyCatalog}
            unresolvedArtifactCount={unresolvedArtifactCount}
            exactRevisionCompiled={exactRevisionCompiled}
            hasPublishedStudy={Boolean(activePublishedStudy)}
            validationResult={exactRevisionValidation}
            validating={validating}
            onDismissOperation={() => {
              setCaseOperationError('')
              setCaseOperationStatus('')
            }}
            onEdit={editCase}
            onValidate={validateCase}
            onInspectComponent={inspectComponentDefinition}
            onInspectDiagnostic={inspectDiagnosticOnCanvas}
            onPublishStudy={() => setAddingStudy(true)}
            onCreate={() => setAddingCase(true)}
            onStudyPackage={openStudyPackageWorkbench}
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
            comparisonJobs={comparisonJobs}
            validationCampaigns={validationCampaigns}
            comparison={resultComparison}
            comparisonLoading={comparisonLoading}
            comparisonError={comparisonError}
            validationReport={validationReport}
            validationReportLoading={validationReportLoading}
            validationReportError={validationReportError}
            onRetry={() => {
              void retryResult()
            }}
            onCompare={(candidateJobId) => {
              void compareResultJob(candidateJobId)
            }}
            onClearComparison={() => {
              setResultComparison(undefined)
              setComparisonError('')
            }}
            onGenerateValidationReport={(campaignRevisionId, jobIds) => {
              void generateValidationReport(campaignRevisionId, jobIds)
            }}
            onExportValidationReport={exportValidationReport}
            onClearValidationReport={() => {
              setValidationReport(undefined)
              setValidationReportError('')
            }}
          />
        )}
        </Suspense>

        <aside
          className={`palette${
            workspaceView === 'topology' ? ' topology-sidebar' : ''
          }`}
        >
          {workspaceView === 'topology' ? (
            <ComponentLibrary
              components={effectiveCatalog?.components ?? []}
              disabled={!topology || publishing}
              catalogFingerprint={effectiveCatalog?.fingerprint ?? ''}
              onChoose={(component) => beginComponentDraft(component)}
              onGroupComponents={() => setAddingAssembly(true)}
              assemblyTemplates={assemblyTemplates}
              onInstantiateAssembly={setInstantiatingAssemblyTemplate}
              onCreateTopology={
                selectedProjectId && !selectedRevisionId
                  ? () => {
                      void createInitialTopology()
                    }
                  : undefined
              }
              onRevise={(component) => {
                const sourceRevisionId =
                  component.source_artifact_revision_id
                const entry = projectComponents.find(
                  (candidate) =>
                    candidate.source.artifact_revision_id === sourceRevisionId,
                )
                if (entry) setRevisingComponent(entry)
              }}
            />
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
              validationCampaigns={validationCampaigns}
              campaignStudyCount={studyRevisions.length}
              calibrations={visibleCalibrations}
              calibrationJobs={calibrationJobs}
              reconciliations={visibleReconciliations}
              reconciliationJobs={reconciliationJobs}
              selectedId={selectedCaseRevisionId}
              publishing={casePublishing}
              canPublishStudy={exactRevisionCompiled}
              onSelect={setSelectedCaseRevisionId}
              onCreate={() => setAddingCase(true)}
              onImportEvidence={() => setAddingValidationSeries(true)}
              onPublishStudy={() => {
                setAddingStudy(true)
              }}
              onPublishValidationCampaign={() => {
                setAddingValidationCampaign(true)
                setRevisingValidationCampaign(undefined)
              }}
              onReviseValidationCampaign={(campaign) => {
                setAddingValidationCampaign(false)
                setRevisingValidationCampaign(campaign)
              }}
              onPublishCalibration={() => setAddingCalibration(true)}
              onRunCalibration={(revision) => {
                void runCalibration(revision)
              }}
              onPublishReconciliation={() => setAddingReconciliation(true)}
              onRunReconciliation={(revision) => {
                void runReconciliation(revision)
              }}
              onInspectReconciliationResult={(job) => {
                void inspectReconciliationResult(job)
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
          onCancel={() => {
            setNewComponentType(undefined)
            setNewComponentPlacement(undefined)
          }}
          onSubmit={addComponent}
        />
      )}
      {workspaceView === 'topology' && showingTopologyJson && (
        <TopologyJsonWorkbench
          key={selectedRevisionId || 'new-topology-json'}
          topology={topology}
          revision={selectedRevision}
          publishing={publishing}
          onCancel={() => setShowingTopologyJson(false)}
          onPublish={publishTopologyDocument}
        />
      )}
      {workspaceView === 'topology' && addingAssembly && topology && (
        <AssemblyForm
          topology={topology}
          onCancel={() => setAddingAssembly(false)}
          onSubmit={groupComponents}
        />
      )}
      {workspaceView === 'topology' &&
        publishingAssemblyTemplate &&
        topology && (
          <AssemblyTemplateForm
            assembly={publishingAssemblyTemplate}
            topology={topology}
            artifactRevisions={artifactRevisions}
            onCancel={() => setPublishingAssemblyTemplate(undefined)}
            onSubmit={publishAssemblyTemplate}
          />
        )}
      {workspaceView === 'topology' &&
        instantiatingAssemblyTemplate &&
        topology && (
          <AssemblyTemplateInstanceForm
            template={instantiatingAssemblyTemplate}
            topology={topology}
            onCancel={() => setInstantiatingAssemblyTemplate(undefined)}
            onSubmit={instantiateAssemblyTemplate}
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
      {workspaceView === 'definition' &&
        (addingBalanceUncertainty || revisingBalanceUncertainty) &&
        topology && topologyCatalog && (
          <BalanceUncertaintyArtifactForm
            key={revisingBalanceUncertainty
              ? `revise-uncertainty-${revisingBalanceUncertainty.source.artifact_revision_id}`
              : 'new-balance-uncertainty'}
            topology={topology}
            catalog={topologyCatalog}
            artifactRevisions={artifactRevisions}
            base={revisingBalanceUncertainty}
            onCancel={() => {
              setAddingBalanceUncertainty(false)
              setRevisingBalanceUncertainty(undefined)
            }}
            onSubmit={publishBalanceUncertainty}
          />
        )}
      {(workspaceView === 'definition' || workspaceView === 'studies') &&
        addingValidationSeries && (
          <ValidationSeriesArtifactForm
            onCancel={() => setAddingValidationSeries(false)}
            onSubmit={publishValidationSeries}
          />
        )}
      {workspaceView === 'studies' &&
        (addingValidationCampaign || revisingValidationCampaign) && (
          <ValidationCampaignForm
            key={revisingValidationCampaign
              ? `revise-campaign-${revisingValidationCampaign.source.artifact_revision_id}`
              : 'new-validation-campaign'}
            studies={studyRevisions}
            campaigns={validationCampaigns}
            base={revisingValidationCampaign}
            onCancel={() => {
              setAddingValidationCampaign(false)
              setRevisingValidationCampaign(undefined)
            }}
            onSubmit={publishValidationCampaign}
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
            validationSeries={validationSeries}
            artifactRevisions={artifactRevisions}
            transient={
              selectedCaseRevision.mode.includes('dynamic') ||
              selectedCaseRevision.mode.includes('transient')
            }
            onCancel={() => setAddingStudy(false)}
            onSubmit={publishStudy}
          />
        )}
      {showingStudyPackage && (
        <StudyPackageWorkbench
          key={studyPackageInitial?.package_id ?? 'study-package-import'}
          initial={studyPackageInitial}
          publishing={studyPackagePublishing}
          onCancel={() => setShowingStudyPackage(false)}
          onImport={importStudyPackage}
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
      {addingReconciliation && selectedRevisionId && (
        <ReconciliationPublishForm
          modelRevisionId={selectedRevisionId}
          studies={visibleStudies}
          cases={caseRevisions}
          onCancel={() => setAddingReconciliation(false)}
          onSubmit={publishReconciliation}
        />
      )}
      {showingReconciliationResult && (
        <ReconciliationResultPanel
          result={reconciliationResult}
          loading={reconciliationResultLoading}
          error={reconciliationResultError}
          onClose={() => setShowingReconciliationResult(false)}
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
