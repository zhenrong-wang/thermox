import type {
  CaseDocument,
  CaseEditOperation,
  CaseScalarField,
  ScalarValue,
} from './types'

export function buildMetadataEdits(
  base: CaseDocument['case'],
  label: string,
  mode: string,
): CaseEditOperation[] {
  const operations: CaseEditOperation[] = []
  const normalizedLabel = label.trim()
  if (normalizedLabel !== (base.label ?? '')) {
    operations.push(
      normalizedLabel
        ? { action: 'upsert', field: 'label', value: normalizedLabel }
        : { action: 'remove', field: 'label' },
    )
  }
  if (mode !== base.mode) {
    operations.push({ action: 'upsert', field: 'mode', value: mode })
  }
  return operations
}

export function buildScalarEdit(
  field: CaseScalarField,
  key: string,
  rawValue: string,
  unit: string,
): CaseEditOperation {
  const normalizedKey = key.trim()
  const numericValue = Number(rawValue)
  if (!normalizedKey || !Number.isFinite(numericValue)) {
    throw new Error('A scalar key and finite numeric value are required.')
  }
  const normalizedUnit = unit.trim()
  const value: ScalarValue = normalizedUnit
    ? { value: numericValue, unit: normalizedUnit }
    : numericValue
  return {
    action: 'upsert',
    field,
    key: normalizedKey,
    value,
  }
}
