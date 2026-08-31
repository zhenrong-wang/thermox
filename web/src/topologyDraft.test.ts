import { describe, expect, it } from 'vitest'
import {
  reviewTopologyDraftSource,
  topologyDraftDefinition,
} from './topologyDraft'

describe('topology draft declaration', () => {
  it('accepts a partial JSON object while keeping promotion blocked', () => {
    const review = reviewTopologyDraftSource(
      JSON.stringify({ model: { id: 'early-cycle', components: [] } }),
    )
    expect(review.document).toBeDefined()
    expect(review.promotable).toBe(false)
    expect(review.promotionIssues).toContain(
      'schema_version must be thermox.topology/v1.',
    )
  })

  it('rejects invalid JSON before draft persistence', () => {
    const review = reviewTopologyDraftSource('{')
    expect(review.document).toBeUndefined()
    expect(review.syntaxIssue).toBeTruthy()
  })

  it('wraps the source without modifying its declaration shape', () => {
    const source = { model: { id: 'cycle' } }
    expect(topologyDraftDefinition('draft-cycle', 'Cycle draft', source)).toEqual({
      schema_version: 'thermox.topology_draft/v1',
      id: 'draft-cycle',
      label: 'Cycle draft',
      document: source,
    })
  })
})
