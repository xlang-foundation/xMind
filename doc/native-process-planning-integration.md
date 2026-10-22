# Native process planning integration

Status: implementation design, 2026-10-21. The native planning service, signed
planning claims and plain continuation funding exist. Production process
planning is not assembled. This document defines the remaining integration;
it does not qualify any missing behavior as complete.

## Ownership and SDK surface

Follow Cantor's registered root and native service objects. The existing
`xmind.Planning().Open(ticket)` returns a lease-bound planning service with
`Invoke` and `Resume`; asynchronous operations expose Status, Result and Cancel.
A worker binding may now expose the same service as
`xmind.Workers().Open(ticket).Planning()`. Attaching it verifies the complete
lease scope; ordinary bindings without that service refuse acquisition. Both
access paths share the same native operation registry.
NativeWorkerModelClient.planning(deadline) now supplies the runner-facing
DynamicPlanExecution port from that subobject, reusing and retaining its SDK
runtime. Distinct-child accepted-context handoff survives outer client retirement;
main WorkspaceExecutor admission and dynamic child scheduling remain unassembled. Distinct-process scope,
duplicate and retired-object checks are verified. Main owner/runner wiring is
still required.

The worker passes stored model-attempt and tool-call identities. Settings,
capabilities, context policy, backend identity and budget authority stay in the
backend. No peer.call planning/execution route or packet fallback is added.
SDK values and remote object handles use xlang3's IPC implementation.

Read/coding owners now retain a planning binding when their backend policy
contains AgentWorkerPlanningServices, attach it to the SDK worker, and pass its
shared budget into the context window. Their bindings close before context
destruction. Distinct-process owner inspection and retirement are verified;
main WorkspaceExecutor construction and the dynamic child runner remain open.

The production dynamic-child factory now reuses WorkspaceExecutor and the
existing supervisor with signed preflight/completion checks. Actual built
xmind.exe child read/completion/drain is verified under the controlled parent
callback. It currently refuses counted/planning-capable child settings without
mutation or fallback. DynamicPlanExecutor selection and wider child-policy
assembly remain open; main process planning admission is still closed.

Dynamic child journal serialization, native adapter admission and shared runner
now work in actual distinct fixture processes. The controlled backend callback
launches the child, verifies an SDK file read and canonical completion, and
waits for physical exit before settlement/parent continuation. Public child
lineage omits backend identity; remote children use backend-owned catalogues.
Production DynamicPlanExecutor selection and counted/coding/nested
child coverage remain open. The local-preset filter is gated to local runners.

Repository now admits dynamic child worker leases against exact signed node,
claim/preset revisions, provider/workspace and open root segment. Typed model
funding/start/final completion and private-identity redaction are ledger-tested
with synthetic child process identity. Production scheduler selection remains required; the supervisor factory now
executes actual xmind.exe children in the controlled accepted-parent contract.

A distinct worker shared-loop test now verifies accepted-plan continuation
funding and final completion: two parent model turns, one synthetic child
charge, exact signed continuation and zero residual holds. Backend acceptance
and synthetic child settlement are controlled; actual DynamicPlanExecutor
agent child dispatch still constructs local runners and must move to processes.

The worker entry point and shared AgentRunner now select the captured SDK
planning port for running dynamic roots. A distinct actual worker loop verifies
initial counted context, canonical inspection, a second counted model and final
completion. Accepted-plan child process scheduling, rejection, overflow
and human handoff still require integration evidence. Main WorkspaceExecutor
planning placement remains closed. Initial 19/20 heap failure and unchanged
diagnostic 20/20 run are retained; broad stability is unproven.

The worker AgentRunJournal adapter now admits exact running planning roots
through a healthy native SDK service and exposes signed continuation reservation.
The distinct child uses it for funding/start/finish/final completion. Capability
and segment APIs stay closed; actual child scheduling and main construction remain open.

Captured planning read/coding owners now complete through the existing SDK journal
Complete method using only the final model attempt ID. The backend owns the
segment clock, derives the canonical receipt, checks completed native work, and
atomically closes the root/segment and retires the lease. Distinct-process
positive, scope/cancellation/deadline refusal and SQL rollback checks pass.
Main WorkspaceExecutor still needs dynamic admission and scheduling.

One backend owner retains the workspace, model host, journal, context window,
planning binding and captured root execution budget through physical child
retirement. A planning operation may block on children or human input; it must
not hold a native client mutex or context-window mutex across those waits.
Retirement invalidates access, cancels/joins work, then releases services.

## Admission and initial context

Read/coding contexts admit planning tools only with captured planning services.
The context window requires the exact shared active backend budget. The worker
journal admits a running dynamic root only with a healthy captured SDK planning
object, and the worker entry point passes that port to the shared runner.
Main WorkspaceExecutor still rejects planning settings and must assemble the
finalized capabilities, provider identity, current lease, workspace revision
and active segment/backend identity before enabling placement. Child journal
admission remains based on a durable child record; a root grant cannot create
arbitrary child authority.

The backend owns one RootExecutionBudget for the dynamic root, including its
active segment clock. ContextManager and DynamicPlanExecutor must use that
same budget. Do not construct an independent ordinary context budget, overwrite
native.dynamic-plan with native.context, or drop the backend segment for convenience.
The child runner has only a public budget/deadline projection, with every
mutation routed through its typed journal; it never owns an active segment.
Initial counted context uses ContextManager's ordinary initial preparation;
the child receives the resulting prepared request and inference identity.

## Accepted continuation and accounting

After a backend-confirmed planning tool call, claim_agent_worker_planning
atomically records entered. Native acceptance links the exact native call;
execution commits the signed conversation turn before continuation funding.
Uncertain or entered calls cannot fund another model. Rejection commits its
canonical rejected tool turn; it is not an accepted plan reservation.

The backend now has commit_agent_worker_plan_inspection: exact lease/attempt
authority derives and atomically commits a native report and inspected claim,
without changing budget holds. Duplicate observation reuses the committed
report. The no-plan case, rollback and stale/corrupt record refusals are covered
by the ledger; existing-plan and cross-process inspection coverage and owner
wiring remain required. This operation must be called by the captured native
planning owner, never supplied an authoritative report by the worker.

ContextManager already uses ContextOwner.planning_call_id and
RootExecutionBudget.reserve_continuation for signed planning preparations.
The shared budget now accepts a backend-only ContinuationReserve function.
Worker owners capture their exact lease/deadline/stop tokens in that function
and forward to PersistenceService.reserve_agent_worker_planning_continuation.
The ledger proves this path and SDK child reuse of its exact reservation. The
existing unscoped journal route continues to refuse worker-linked plans. This
function is an internal reservation dependency, not a worker packet dispatcher;
production context-window and planning owners still need to share that budget.

The context window now has explicit typed planning preparation/reservation
operations and the journal has a corresponding hook. They set the planning
call on ContextOwner, prepare through the existing ContextManager and retain
its exact inference step for start/finish. The distinct SDK child ledger case
verifies this with unknown capacity and automatic compaction disabled. Token
counting/overflow/compaction tests and production owner wiring remain required.
Context-managed journals without this hook continue to refuse plain planning
funding.
The owner callback wrapper now commits accepted native reports before typed
context preparation. Generic preparation refuses pending signed continuations;
combined accepted-owner context funding now has a distinct SDK child case
using synthetic settled children and a controlled accepted-report callback.
The child obtains the backend-selected attempt through the existing journal
method with an empty hint; plain unprepared funding still requires an ID.
Positive production DynamicPlanExecutor/worker-child scheduling remains required. Planning overflow must hand
out the backend rebuilt physical attempt and recognize successful logical
inference even when the call retains its original continuation pointer. Current
worker gates still close this retry/adoption path; verify it explicitly before
enabling placement.

Never reserve once through the plain endpoint and again through ContextManager.
The worker cannot select a different attempt after context preparation.

## Runner and child scheduling

Add a planning journal/runner assembly rather than opening every denied method
on OrdinaryJournal. Expose only the durable projections and lifecycle operations
required by AgentRunner's dynamic path. Canonical assistant, tool turn, budget
and pause facts come from the backend. The native planning client identifies
facts; it cannot supply authority-bearing capabilities or settings.

make_agent_worker_planning_callbacks now resolves stored model/call facts
under captured admission and delegates plan/revise execution to the retained
DynamicPlanExecution port. Its distinct SDK inspection and controlled rejection
paths are verified. Main owner/runner construction still needs to instantiate
it with the backend DynamicPlanExecutor. Its agent nodes must dispatch through
the existing process supervisor
and capacity owner, not local AgentRunner instances. Deterministic tools continue
through backend effect/permission owners. A completed durable node does not free
physical process capacity until its exact process has drained. Cancellation or
lost result never authorizes replay of an uncertain effect.

## Pause and resume

Human waits suspend the complete dynamic owner path and active segment using
existing durable pause records. A later owner/segment obtains new lease-bound
services. Retained objects from the old generation refuse all operations.
Resume reads committed child and conversation facts; it cannot rerun completed
nodes or continue the old segment clock. Counted context resumes from the signed
context pin and exact protected user groups, using the existing ContextManager
checks before funding another model.

## Required evidence before enabling placement

1. Actual distinct root process: initial context, accepted plan, actual worker
   child, signed tool turn, exactly one counted continuation and final receipt.
2. Canonical rejection and inspection paths, without spending accepted-plan
   holds; changed call/attempt, stale generation and foreign scope refuse.
3. Context overflow/compaction before and after planning, exact inference links,
   one charge per attempt, atomic rollback and no plaintext credentials in IPC.
4. Parallel/nested children share root capacity and budgets. Observation timeout
   retains physical ownership; cancellation joins captured processes.
5. Human pause/resume across new lease/segment, exact history and context pin,
   stale handles denied, no replay of completed model/tool tasks.
6. Full current native suite plus installed CLI, browser and extension coding
   acceptance, each recorded at its actual scope. Focused or synthetic results
   do not establish installed release completion.

Implementation order follows admission/budget ownership, context preparation,
planning journal/runner, process child scheduling, then pause/resume and client
acceptance. Keep process planning placement closed until the applicable paths
are assembled and verified; remove superseded adapters when their callers move.
