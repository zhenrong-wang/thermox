# Result projections

Thermox result summaries must remain independent of any particular cycle or component library.
The platform therefore does not define built-in outputs such as gas-turbine heat rate, Rankine
efficiency, or reactor thermal margin. Those are model- and user-selected result values.

The service-level projection contract selects values from the stable `GraphResult` namespaces:

- system balances and KPIs;
- component metrics and internal values;
- port primary and derived values.

Every selector names its expected physical dimension. Projection fails if the selected value is
missing or its dimension differs, preventing a report definition from silently binding to a
different quantity after a model revision.

Composition-aware material ports expose both their individual species-flow primary values and
generic derived values: `m_dot_total` plus `mass_fraction[species]` for every declared species.
This keeps fuel, air, extraction, and exhaust totals selectable without embedding a particular
reference species or mixture composition in service/UI post-processing.

Steady projections use the final solved graph. Transient projections select `final`, `minimum`,
`maximum`, `mean`, `root_mean_square`, or `change`. Change is the signed end value minus the start
value and preserves the selected physical dimension. A projection can reduce the complete
trajectory, an absolute simulation-time window, or a window anchored to a named event occurrence.
Exact window boundaries and event-relative offsets are canonical SI seconds and are linearly
interpolated from graph-native samples. Event occurrences are zero-based. Extrema and final values
retain
their evaluation time; change retains the end time, while mean and RMS values retain the resolved
window instead of inventing a representative sample time.

Event-relative windows currently require non-negative offsets. An offset of zero evaluates the
post-transition sample recorded at the event. `thermox.result/v6` retains the ordinary graph as the
right limit and adds `graph_before_discontinuity` as the left limit whenever a trajectory sample
follows a scheduled input jump or event transition. Interpolation approaching that timestamp ends
at the left limit, crosses the jump with zero duration, and resumes from the right limit. It never
smears an instantaneous change across the preceding integration interval.

Minimum and maximum reductions inspect both limits. Mean uses trapezoidal integration of each
continuous piece, while RMS uses the exact integral of the square of each linear piece; the jump
itself contributes zero duration. A window beginning exactly at a jump uses only the right limit,
and a window ending at one includes both limits for extrema while remaining measure-neutral for
mean and RMS. Change uses the right-limit value when its window begins at a discontinuity, making
scheduled step-response increments unambiguous. All reductions preserve the selected value's
physical dimension.

Projection definitions are stored in immutable run-configuration revisions and participate in
their checksums. Submission snapshots them into the immutable job request. After a successful
solve, the worker materializes `thermox.result_summary/v4` before writing the full result artifact,
then publishes the summary, artifact manifest, and terminal job revision atomically. A missing or
dimensionally incompatible selector produces a structured result-stage job failure.

This keeps summary policy owned by the run definition rather than the HTTP API, worker host, or a
particular UI. Run-history and status responses can display the compact summary without loading or
parsing the full result artifact from object storage.

Comparisons align multiple selected signals by projection ID. Dimension, aggregation, and resolved
window evidence must all match before Thermox reports a numerical delta; incompatible windows are
reported explicitly rather than compared as if they represented the same engineering quantity.

The thin Results workspace displays the compact summary as the run's configured reductions. It
requests the full `thermox.result/v6` artifact only when a user selects a succeeded job, then joins
the immutable projection selectors to the current steady graph or selected transient sample for
the node overlay. The same graph feeds system-balance, KPI, component, internal-state, and
port/stream tables. The browser does not derive thermal-cycle semantics: custom components,
connector domains, and result dimensions remain first-class because the presentation follows
stable graph identities.

The Results workspace flattens those namespaces into a stable identity index for text and scope
filtering. It can export the visible sample or every transient sample as long-form CSV. Exported
values remain canonical SI and include the catalog-declared canonical unit symbol; display-profile
conversion is deliberately not applied to the audit artifact. Transient plotting selects any
finite indexed signal, retains its physical dimension for catalog-driven display, and marks the
sample currently projected onto the topology. The browser does not interpolate missing samples or
invent values.

Result artifacts and summaries remain canonical SI. The runtime unit registry publishes SI and
engineering display descriptors through `thermox.catalog/v13`. A browser-local display profile
applies those reversible dimension-keyed conversions to summaries, overlays, tables, and
transient derivatives; offset units apply their offset only to absolute values. Unknown
dimensions are shown unchanged rather than assigned a guessed unit. The browser's built-in table
is only an unavailable-catalog fallback and does not change result persistence or platform unit
semantics.
