# Native process planning integration

Status: implementation design, 2026-10-21. The native planning service, signed
planning claims and plain continuation funding exist. Production process
planning is not assembled. This document defines the remaining integration;
it does not qualify any missing behavior as complete.

## Ownership and SDK surface

Follow Cantor's registered root and native service objects. The existing
`xmind.Planning().Open(ticket)` returns a lease-bound planning service with
`Invoke` and `Resume`; asynchronous operations expose Status, Result and Cancel.
The worker passes stored model-attempt and tool-call identities. Settings,
capabilities, context policy, backend identity and budget authority stay in the
backend. No peer.call planning/execution route or packet fallback is added.
SDK values and remote object handles use xlang3's IPC implementation.

One backend owner retains the workspace, model host, journal, context window,
planning binding and captured root execution budget through physical child
retirement. A planning operation may block on children or human input; it must
not hold a native client mutex or context-window mutex across those waits.
Retirement invalidates access, cancels/joins work, then releases services.

## Admission and initial context

AgentWorkerReadContext currently admits ordinary/context/delegation policies;
AgentWorkerContextWindow additionally admits graph children. OrdinaryJournal
and execute_workspace_root also reject dynamic roots. These are separate
checks, not one flag to remove. Introduce a distinct captured planning admission
for the exact native.dynamic-plan root. It requires finalized capabilities,
provider identity, current lease, matching workspace revision and the root's
active budget segment/backend identity. Child journal admission remains based
on a durable child record; a root grant cannot create arbitrary child authority.

The backend owns one RootExecutionBudget for the dynamic root, including its
active segment clock. ContextManager and DynamicPlanExecutor must use that
same budget. Do not construct an independent ordinary context budget, overwrite
native.dynamic-plan with native.context, or drop the segment for convenience.
Initial counted context uses ContextManager's ordinary initial preparation;
the child receives the resulting prepared request and inference identity.

## Accepted continuation and accounting

After a backend-confirmed planning tool call, claim_agent_worker_planning
atomically records entered. Native acceptance links the exact native call;
execution commits the signed conversation turn before continuation funding.
Uncertain or entered calls cannot fund another model. Rejection commits its
canonical rejected tool turn; it is not an accepted plan reservation.

ContextManager already uses ContextOwner.planning_call_id and
RootExecutionBudget.reserve_continuation for signed planning preparations.
That budget currently calls the unscoped AgentRunJournal reservation, which
correctly refuses worker-linked plans. The integration must supply an internal,
lease-bound journal implementation for this reservation, forwarding to
PersistenceService.reserve_agent_worker_planning_continuation with the captured
lease/deadline/stop tokens. This adapter stays in the backend, is not a worker
packet dispatcher, and does not authorize arbitrary journal calls.

Add an explicit typed planning preparation/reservation operation to the backend
context window and its journal hooks. It must set the planning call on the
ContextOwner, prepare/count/compact through the existing ContextManager, and
hand out that preparation's exact attempt. Journal start/finish use the same
prepared inference step and canonical backend assistant. Preserve the current
refusal for context-managed planning funding until the complete hook is wired.
Never reserve once through the plain endpoint and again through ContextManager.
The worker cannot select a different attempt after context preparation.

## Runner and child scheduling

Add a planning journal/runner assembly rather than opening every denied method
on OrdinaryJournal. Expose only the durable projections and lifecycle operations
required by AgentRunner's dynamic path. Canonical assistant, tool turn, budget
and pause facts come from the backend. The native planning client identifies
facts; it cannot supply authority-bearing capabilities or settings.

Wire NativePlanningCallbacks to the backend DynamicPlanExecutor using captured
admission. Its agent nodes must dispatch through the existing process supervisor
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
