# Nested dynamic planning contract

Status: **partly implemented**. Schema v24 records immutable plan ownership;
v25 permits multiple plans per root; v26 stores owner-scoped signed calls.
The runtime now lets an admitted dynamic-agent child own one nested plan,
using the same root budget and child pool. Nested plan depth beyond that and
nested human waits remain unsupported.

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
Focused synthetic-provider contracts exercise root → outer agent → nested plan
→ inner agent, verify owner links and actual read history, and confirm the
shared root budget. A nested human node is rejected with
`nested_human_wait_unsupported`; plans below the supported nested level are
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

Schemas v24-v26 provide the durable ownership foundation: plans carry an
immutable owner, multiple plans may share a root, and signed calls carry the
same owner with per-owner idempotency and pending-call constraints. The v26
migration preserves existing call, event and plan identities transactionally
and validates foreign keys. Runtime admission and execution still need to
prove the owner is a live claimed child and carry ownership through model
turns, scheduling, recovery and presentation.

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
in-process worker. The supported nested plan is depth one beneath the ordinary
root plan; a deeper planning call receives a typed rejection.

A parent plan node joins its child only after the child-owned plan reaches a
finished report, and repository completion guards prevent a running child
from completing around unfinished plan work. Nested human waits are not
accepted: they need a durable continuation stack identifying every owning
plan/call/node, plus resume behavior that reopens that exact chain and preserves
child context pins and the root's remaining active-time segment. Restart
recovery must settle from actual child/event/effect evidence and must never
reconstruct or replay a model plan from a prompt.

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
3. **Implemented for one nested level:** admit a plan only from an exact
   claimed dynamic-agent child; route its tools through the same executor and
   child pool; preserve root budget ownership and per-owner continuation
   receipts; and verify a two-level plan with synthetic provider turns and
   real SQLite child history. The contract also checks typed rejection of a
   nested human wait.
4. Add durable pause/resume and crash-recovery for nested human waits and
   unresolved effects. Verify root restart drains or preserves every nested
   owner without duplicate provider dispatch or effect replay.
5. **Partly implemented:** `/v1/runs/{root}/plan` now returns depth-one owner
   bindings, each child run, its current plan topology, accepted revisions and
   call receipts/available held-response metrics. The strict shared browser/
   VS Code view renders each nested plan under the exact admitting parent task.
   Repository and HTTP contracts verify the owner link and projection. Nested
   owner histories, completed-response metrics, CLI rendering, depth/total-
   work exhaustion and parallel sibling plans remain pending.

The executor, owner lifecycle and nested-provider acceptance stages passed
the focused Release set **3/3** (`native_dynamic_plan_contract`,
`native_dynamic_plan_repository_contract`, and
`native_dynamic_plan_engine_contract`). The engine peer is synthetic; this
does not prove live-provider behavior. Nested-plan HTTP and shared client
presentation are now implemented with synthetic DTO/HTTP contracts. Durable
nested pause/restart recovery, nested owner histories, completed-response
metrics, CLI presentation and live-provider behavior remain incomplete.
See [current implementation](native-dynamic-plan.md),
[broader planning design](native-dynamic-plan-design.md) and
[OpenCode parity inventory](PARITY.md).
