# Nested dynamic planning contract

Status: **partly implemented**. Schema v24 records immutable plan ownership;
v25 permits multiple plans per root; v26 stores owner-scoped signed calls; and
v27 admits bounded descendant ownership through depth eight. Dynamic-agent
children can create nested plans under the same root budget and child pool.
Depth-two execution is covered end to end. Nested human waits and durable
nested pause/restart recovery are implemented for one waiting owner chain and
covered through a real service close/reopen. Simultaneous nested waits in
sibling branches remain unsupported and are rejected before plan acceptance.

## Current boundary

One `dynamic_capabilities` row is admitted for an ordinary Agent root. Schema
v25 permits multiple `dynamic_plans` rows under that root, and v26 stores
owner-scoped signed calls. The repository admits a nested signed plan only
for the exact running dynamic-agent child claimed by its active parent plan;
it records the parent plan/node/depth and preserves aggregate root child and
continuation reservations. At runtime, dynamic children expose owner-scoped
planning tools, execute nested plans through the shared child pool, and return
their results to the parent plan. Completion is refused until the child-owned
plan is finished, then both the run and plan are completed transactionally.
Focused synthetic-provider contracts exercise root → outer agent → inner
planner → depth-two reader, verify owner links and actual read history, and
confirm the shared root budget. A nested human node is rejected with
`nested_human_wait_unsupported`; a plan beyond the supported depth of eight is
rejected with `planning_depth_exhausted`.

The runtime already gives a dynamic child the ordinary root's
`RootExecutionBudget`, and model reservations identify both `root_run_id` and
`owner_run_id`. That is the foundation to retain: a nested plan must never
create a new execution budget or reset model, child, parallelism, deadline,
context, or cancellation allowances.

## Required ownership model

Keep two identities distinct throughout persistence and execution:

* `root_run_id` owns the single durable execution budget and global limits.
* `owner_run_id` is the exact ordinary root or admitted dynamic-agent child
  whose model turn selected a plan change.

Each plan is unique to an `owner_run_id`, while any number of descendant plans
may reference the same `root_run_id`. A nested plan also records the immutable
parent plan and claimed agent-node identity that admitted its owner. A child
cannot create a plan merely because it shares a session or knows a run ID.
Repository admission must re-derive that the owner is the exact live child of
the recorded parent node, and that both plans belong to the same root.

Plan calls must bind the actual assistant/tool-call receipt and finished model
reservation for that `owner_run_id`. Idempotency and the one-pending-call rule
are scoped to the plan owner, not the entire root; sibling planned agents may
run concurrently, but one owner cannot accept overlapping plan mutations.
Plan labels and revisions remain local to their owning plan. All topology,
claims, children, human requests, results and history links retain both owner
and root identity.

Schemas v24-v27 provide the durable ownership foundation: plans carry an
immutable owner, multiple plans may share a root, signed calls carry the same
owner with per-owner idempotency and pending-call constraints, and descendant
owners bind directly to the ordinary root while retaining immutable parent
plan/node links. The v26 migration preserves existing call, event and plan
identities transactionally and validates foreign keys; v27 replaces the
single-nested-owner admission trigger with bounded descendant validation.
Runtime admission and execution validate the live claimed owner and carry its
identity through model turns, scheduling and presentation. Nested pause and
restart recovery remain pending.

Capabilities remain derived from the root's immutable admitted settings and
the exact coding preset. Nested plans may select only registered presets and
tools already within that authority. They cannot introduce provider routes,
credentials, workspaces, profiles, MCP/A2A registrations, effect grants or
controller actors. Every effect continues through the existing approval and
operation journals.

## Runtime and recovery rules

An admitted dynamic-agent child may receive `plan_tasks`, `revise_plan` and
`inspect_plan` within its captured preset authority. `AgentRunner` derives
owner, root and plan identities from the admission record; none comes from
model arguments. Nested model calls retain their child `owner_run_id` and
common `root_run_id`, and the same budget object is passed to every descendant
in-process worker. Plans may be nested to depth eight beneath the ordinary
root plan; a deeper planning call receives a typed rejection. The runtime
temporarily yields the calling child’s active leaf slot while it schedules a
nested plan, so bounded root parallelism applies to active leaf work rather
than counting idle planner ancestors as active leaves.

A parent plan node joins its child only after the child-owned plan reaches a
finished report, and repository completion guards prevent a running child
from completing around unfinished plan work. For a nested human wait, the
repository binds the deepest waiting plan/call to the exact root-to-child owner
chain, pauses each admitted owner and closes the shared active-time segment in
one transaction. An authenticated answer resumes that same chain and segment;
after SQLite reopen, recovery preserves the owner chain only when its accepted
calls, claimed parent nodes, closed segment and effects all validate. No plan
or child is reconstructed from a prompt, and accepted provider calls are not
replayed. The current single-chain binding rejects a nested human wait while a
sibling root child remains live; multi-branch pause coordination is still open.

The current synchronous call stack makes this a root-wide pause, not an
independent child pause. A nested `DynamicPlanExecutor` runs inside its admitted
agent child's `AgentRunner`, which in turn is a future owned by its parent
executor. Returning a paused child directly currently fails the parent's
terminal-child settlement check; after restart, the in-memory future and active
batch no longer exist, and the executor deliberately refuses to adopt that
claimed node. A correct implementation therefore has to persist the complete
owner chain from the ordinary root plan to the waiting plan, then update the
child executor's join rules so only a database-proven terminal child outcome
can be settled after restart.

The durable pause must bind the waiting plan and accepted call to each ancestor
plan node and its claimed child run. The pause transaction must close the one
root budget segment, preserve the exact context snapshot used by the waiting
owner, and ensure every owner in the continuation chain is paused with no live
child work, model attempt, or unresolved effect. Independent sibling work must
either reach a journaled terminal result before that transaction commits or
prevent the pause; it cannot be silently abandoned. On restart, the service
must validate the whole chain against plan ownership, claim receipts, run
states, human answers, context bindings, budget segments and effect journals.
Resume proceeds from the deepest waiting plan outward, using the already
accepted call and actual child results. It must never reissue an accepted
planning call, recreate a child, or redispatch an uncertain effect. If any
binding is stale or incomplete, retain the paused run for inspection and
cancellation rather than guessing at a continuation.

Bounds are root-wide as well as per-plan: nested depth, total planned
nodes/revisions/human questions, model turns, active descendants and
wall/active time must be checked transactionally against the one root budget.
An exhausted bound is a durable typed rejection before acceptance. Once a
plan call is accepted, an execution or settlement failure remains owned and
inspectable; it cannot be converted into an ordinary tool error.

## Implementation and acceptance sequence

1. **Implemented in schema v24:** add an immutable SQLite plan-owner relation
   with `owner_run_id`, parent-plan/node linkage and depth. Backfill every
   existing plan to `owner_run_id = root_run_id`; reopen and integrity-check
   populated v23 databases.
2. **Partly implemented:** repository and persistence service expose
   owner-scoped plan lookup, schema v25 permits multiple plans per root, and
   schema v26 persists owner-scoped signed-call identity, pending-call
   admission and owner-bound receipts/continuations. Repository acceptance
   validates a live claimed dynamic-agent owner, binds the nested plan to its
   immutable parent node/depth, and adds reservations to the shared root
   totals. The focused contract covers a valid nested owner, mismatched-root
   rejection, root-wide call enumeration and aggregate child/continuation
   reservations. The actual nested invocation and owner lifecycle are covered
   in stage 3; broader forged/stale-owner cases remain pending.
3. **Implemented through bounded depth eight:** admit a plan only from an exact
   claimed dynamic-agent child; route its tools through the same executor and
   child pool; preserve root budget ownership and per-owner continuation
   receipts; and verify root → outer planner → inner planner → depth-two reader
   with synthetic provider turns and real SQLite child history. Engine
   contracts also answer a nested human question, resume its accepted owner
   call, close/reopen the backend during a nested pause and finish without
   replay. A nested human wait with another live root child receives the typed
   pre-acceptance rejection `nested_human_wait_parallel_unsupported`. The repository contract
   now admits plans through depth eight under the real eight-child shared root
   budget and checks all nine persisted plan depths plus the eight exact child
   admissions. Repository acceptance now counts plan nodes, accepted revisions
   and planned/published human questions across every descendant owner inside
   the serialized root transaction. Focused contracts verify that each shared
   limit rejects a nested plan without leaving partial owner or reservation
   state. The repository contract verifies the supported depth-zero through
   depth-eight chain and all eight lifetime child admissions. A ninth owner is
   unreachable through valid admission because each nested level consumes one
   of the root's eight child slots; the executor's depth-nine guard therefore
   has no honest end-to-end fixture. Remaining total-work bounds remain
   pending.
4. **Implemented for one nested owner chain:** durable pause/resume and restart
   recovery preserve the exact waiting plan/call, paused descendant runs and
   original continuation receipts. Extend this to multiple sibling nested
   human waits and broader unresolved-effect restart cases.
5. **Partly implemented:** `/v1/runs/{root}/plan` returns owner bindings for
   nested plans through the supported depth, each child run, its current plan
   topology, accepted revisions and call receipts/available held-response
   metrics. The shared browser/VS Code
   view renders each nested plan and its actual owner/child transcripts under
   the admitting parent task, retaining each response's supplied metrics. The
   native `inspect-plan` command prints this same authenticated observation as
   formatted JSON. Repository, HTTP, client and renderer contracts verify the
   owner links and projection. The repository exercises the reachable
   depth-eight boundary under the shared child budget; additional total-work
   bounds and parallel nested-plan scheduling remain pending. Root-wide node,
   revision and human-question bounds are covered by the repository contract.

The executor, owner lifecycle and nested-provider acceptance stages passed
the focused Release set **3/3** (`native_dynamic_plan_repository_contract`,
`native_planning_http_contract`, and `native_dynamic_plan_engine_contract`).
The VS Code extension suite passed **346/346**. The engine peer is synthetic; this
does not prove live-provider behavior. Nested-plan HTTP and shared client
presentation, including actual descendant transcripts and response metrics,
are implemented and tested. The CLI uses the same JSON endpoint/output path;
its presentation remains structured JSON rather than a dedicated tree view.
Durable nested pause/restart recovery, remaining total-work exhaustion,
parallel nested-plan scheduling and live-provider behavior remain incomplete.
See [current implementation](native-dynamic-plan.md),
[broader planning design](native-dynamic-plan-design.md) and
[OpenCode parity inventory](PARITY.md).
