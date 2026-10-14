# Nested dynamic planning contract

Status: **not implemented**. This contract is based on the current native
schema and execution paths. Schema v24 now records immutable plan ownership
and backfills existing plans to their ordinary Agent roots; the runtime still
does not admit nested plans or give dynamic child agents planning tools.

## Current boundary

One `dynamic_capabilities` row and one `dynamic_plans` row are admitted for an
ordinary Agent root. `dynamic_plans.root_run_id` is unique;
`dynamic_plan_calls` and the pending-call index are keyed at that root;
signed-call triggers require a finished `parent` reservation owned by the
root. `DynamicPlanExecutor::invoke` and `resume` reject child owners.
`AgentRunner` only exposes plan tools to an ordinary dynamic root, and
`dynamic_child_settings` clears planning and delegation. These are enforced
source boundaries, not just documentation policy.

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

Capabilities remain derived from the root's immutable admitted settings and
the exact coding preset. Nested plans may select only registered presets and
tools already within that authority. They cannot introduce provider routes,
credentials, workspaces, profiles, MCP/A2A registrations, effect grants or
controller actors. Every effect continues through the existing approval and
operation journals.

## Runtime and recovery rules

An admitted coding child may receive `plan_tasks`, `revise_plan` and
`inspect_plan` only when its durable owner binding includes the nested-planning
capability. `AgentRunner` derives owner, root and plan identities from the
admission record; none comes from model arguments. Nested model calls retain
their child `owner_run_id` and common `root_run_id`, and the same budget object
is passed to every descendant in-process worker.

A parent plan node cannot settle while its child has an active nested plan,
pending signed call, running descendant, human wait or unresolved effect. The
root scheduler must join and drain every descendant before root completion,
cancellation, clean pause or shutdown recovery. A nested human wait therefore
needs a durable continuation stack identifying every owning plan/call/node;
resume must reopen that exact chain and preserve child context pins and the
root's remaining active-time segment. Restart recovery must settle from actual
child/event/effect evidence and must never reconstruct or replay a model plan
from a prompt.

Bounds are root-wide as well as per-plan: maximum recursive depth, total
planned nodes/revisions/human questions, model turns, active descendants and
wall/active time must be checked transactionally against the one root budget.
An exhausted bound is a durable typed rejection before acceptance. Once a
plan call is accepted, an execution or settlement failure remains owned and
inspectable; it cannot be converted into an ordinary tool error.

## Implementation and acceptance sequence

1. **Implemented in schema v24:** add an immutable SQLite plan-owner relation
   with `owner_run_id`, parent-plan/node linkage and depth. Backfill every
   existing plan to `owner_run_id = root_run_id`; reopen and integrity-check
   populated v23 databases. Nested owner admission and owner-scoped pending
   call constraints remain unimplemented.
2. **Partially implemented:** repository and persistence service expose
   owner-scoped plan lookup. Add nested admission and test forged owner,
   sibling owner, mismatched root, stale
   admission, duplicate calls and a second root budget.
3. Admit a nested plan only from a claimed coding-agent child. Route its tools
   through the same executor and child pool, preserving root budget ownership
   and per-owner continuation receipts. Verify a two-level plan with actual
   synthetic provider turns and real SQLite child history.
4. Add durable pause/resume and crash-recovery for nested human waits and
   unresolved effects. Verify root restart drains or preserves every nested
   owner without duplicate provider dispatch or effect replay.
5. Render the plan tree, nested owner histories, metrics, human waits and
   uncertainty through the shared browser/VS Code view and inspect the actual
   CLI output. Test depth/total-work exhaustion and parallel sibling plans.

Completion requires all five stages. The current focused dynamic-plan tests
prove root-owned plans only and are insufficient evidence for this contract.
See [current implementation](native-dynamic-plan.md),
[broader planning design](native-dynamic-plan-design.md) and
[OpenCode parity inventory](PARITY.md).
