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

Steady projections use the final solved graph. Transient projections explicitly select `final`,
`minimum`, or `maximum` and retain the time of the selected sample. These reductions are generic;
future integral, average, event, or windowed reductions can extend the contract without changing
the graph or solver.

Projection definitions are stored in immutable run-configuration revisions and participate in
their checksums. Submission snapshots them into the immutable job request. After a successful
solve, the worker materializes `thermox.result_summary/v1` before writing the full result artifact,
then publishes the summary, artifact manifest, and terminal job revision atomically. A missing or
dimensionally incompatible selector produces a structured result-stage job failure.

This keeps summary policy owned by the run definition rather than the HTTP API, worker host, or a
particular UI. Run-history and status responses can display the compact summary without loading or
parsing the full result artifact from object storage.
