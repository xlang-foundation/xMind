# Native dynamic plans and revisioned replanning

The initial `3fc4204` planning source also passed its [exact hosted gate](https://github.com/xlang-foundation/xMind/actions/runs/37808089507): **77 native / 119 extension / 26 browser** tests, all 16 successful steps, model-free integration and 18 verified VSIX assets. Downloaded archives and original logs match their recorded digests. Installation, rendered/live planning and editor acceptance remain separate. [Hosted source and scope evidence](evidence/native-dynamic-plan-hosted-provenance.json).

Status: the initial agent/human planning slice is implemented and locally
validated. The complete guarded **77-contract native gate passed in 153.88
seconds**, with its exact manifest and all **421 frozen source hashes** verified;
shared-view source gates passed **119 extension and 26 browser contracts**.
Actual engines perform dependency work, same-plan revision, authenticated human
pause/reopen and a separately approved coding-child edit with verification;
provider replies/signatures/keys are synthetic. Model-free browser/native
graph/human/auth/cookie/reconnect integration and packaging of all **18 required
VSIX assets** also passed, without rendered dynamic-planning or installed-editor
acceptance. [Initial implemented boundary](native-dynamic-plan.md),
[local source and gate evidence](evidence/native-dynamic-plan-local-provenance.json).

This document retains the broader design. Only agent and human nodes are
accepted in the initial model-selected schema; deterministic tool/MCP/process
nodes, recursive planning, skills, compaction and outbound A2A remain future
work. Schema-v11 installation, live planning and installed VS Code acceptance remain pending. The installed ace/schema-v10
scope below is separate from the new local planning result.

[Bounded native delegation](native-delegation.md) was introduced at historical
revision `e353a37799530a234a6fa13e51f61a5c52d3ae6a`, with scoped local and exact
hosted 71/103/19 gates and
[revision/artifact/pin verification](evidence/native-delegation-hosted-provenance.json).
The separate e353 browser repair passed 103/20 gates. The first live request
failed terminal Responses consistency before admitting children; its outcome
remains recorded in the [earlier acceptance scopes](native-delegation-acceptance.md).
The later 7fe7 diagnostic request isolated encrypted-content-only variation in
that specific request, without establishing the cause of the earlier failure.
[Historical diagnostic evidence](native-responses-diagnostics.md).

The installed native checkpoint is
`ace246094f6c1fc8cf61c146cfe001c63f1bbc8f`: its narrow reasoning continuation
change passed the exact hosted **71 native / 103 extension / 20 browser** gates.
The verified native bundle is installed with schema v10 and the retained
repaired e353 browser view. On **2026-10-08**, a new actual browser request
visibly completed a parent and two read-only children with separate response
metrics. A separate read-only audit of that same completed request passed:
six finished owned model attempts, independent parent/child histories, actual
child settlements and parent continuation, with zero operations. Leaf
responses may omit reasoning; present reasoning remains strictly validated
and the parent's initiating delegation receipt contained nonempty reasoning.
Live terminal opaque values were not inspected, and the audit did not repeat
the model request.
[Live owned-record audit](evidence/live-browser-responses-reasoning-delegation-ace24609.json),
[actual browser completion](evidence/live-browser-responses-reasoning-delegation-ace24609-browser.json),
[managed upgrade scope](evidence/native-responses-reasoning-upgrade-provenance.json),
[current reasoning checkpoint and acceptance scope](native-responses-reasoning.md).

Those ace observations do not establish the newer schema-v11 planning result
or installed planning acceptance. Installed VS Code delegation remains
unverified. The initial local planning milestone above does not redefine the
broader [dynamic Agent target](dynamic-agent-execution.md).

The initial implemented behavior is a normal Agent selecting a dependency
plan, observing child work and human input, then revising undispatched tasks,
with authorized coding work as well as investigation. The native backend owns
the plan, capabilities, budgets and effect journals; thin clients observe those
records and submit authenticated input. Later node kinds and recursion must
preserve these same boundaries.

## Existing foundations and missing boundaries

The table preserves the original delivery boundaries. The initial v11 slice
now implements their agent/human subset; the implementation guide describes
its actual classes and APIs. Direct deterministic nodes and broader graph
integration remain design requirements.

| Existing contract | Reuse | Required addition |
| --- | --- | --- |
| `AgentRunner` model/tool loop | Provider adapters, signed history, instruction policy, exact effect handlers | Planning tools, durable suspended tool-call continuation, resume under the same owner |
| `RootExecutionBudget` and v10 reservations | One root clock, cancellation, model-attempt accounting and held parent allowances | Planned-child reservations and durable active-time accounting for clean human pauses |
| `DelegationExecutor` | Separate bounded child workers, root concurrency, drain and fail-stop behavior | Common agent/tool dispatch with dependency readiness and backend-selected execution presets |
| `GraphPlan` | Strict identifiers, DAG validation and declared-reference checks | Authority-annotated dynamic node schema and literal-preserving argument projection |
| `GraphCoordinator` | Immutable topology/state contracts and observed-output concepts | A distinct coordinator with revision reconciliation, blocked work and failure observations |
| Repository/PersistenceService | xlang3 SQLite, serialized transactions, owned runs/history/events and recovery | Dynamic plan/revision/claim/human/call records and compare-and-swap methods |
| Effect journal and controller routes | Exact proposals, authentic decisions, resource contention and uncertainty | Dynamic-child ownership in the same approval/inspection routes |
| Child/history/tree APIs and shared views | Stable identities, committed cursors, isolated history and metrics | Plan revision, dependency, human-question and protected-node presentation |

`GraphCoordinator` currently requires a checkpoint's specification to equal its
immutable GraphPlan and halts on failed, uncertain or cancelled nodes. Its
`skip` operation requires an actual false condition; it is not a general model
skip command. Calling these existing APIs with a changed specification cannot
implement replanning. Registered GraphRunner also admits only catalogue-backed
graph roots and currently does not pass the ordinary Agent's shared budget to
agent children. These are concrete integration boundaries, not missing UI.

Source anchors for review are [AgentRunner](../Native/src/agent_runner.cpp),
[DelegationExecutor](../Native/src/delegation_executor.cpp),
[RootExecutionBudget](../Native/src/root_execution_budget.cpp),
[GraphCoordinator](../Native/src/graph.cpp),
[Repository](../Native/src/repository.cpp) and
[GraphService's human-pause owner](../Native/src/graph_service.cpp).

## Identity and authority

A dynamic plan belongs to one **ordinary Agent root**. It receives a backend
plan ID and revision; it is stored in new dynamic-plan relations. The root
remains an ordinary run and does not acquire `graph_root: true`. Model plans
must never be imported into GraphCatalogStore or written to `graph_roots` to
bypass trusted registration.

Registered graphs retain their catalogue ID/revision, current immutable
checkpoint rules and graph-specific APIs. Pure DAG/reference helpers can be
shared, but this does not make a model-generated plan a registered graph.
Nested planning inside a registered graph agent needs a later explicit
integration with that graph root's budget and ownership. It must not create an
independent root or reset an allowance.

The backend binds every dynamic task to the root's frozen provider/model,
workspace identity and instruction policy. Its capability snapshot includes
registered preset IDs/revisions, allowed native tools, trusted process profiles
and enabled immutable MCP server bindings. Public records expose identifiers
and revisions; private endpoints, credential references/values and executable
paths remain in the execution generation. Model arguments cannot supply
owners, actors, endpoints, secrets, workspace destinations or approval grants.

The proposed preset registry includes:

| Preset | Native authority |
| --- | --- |
| `workspace.inspect` revision 1 | Existing read-only tools; no effects or recursive delegation |
| `workspace.coding` revision 1 | The parent's authorized read/edit/create tools and only its enabled registered process/MCP tools, each retaining existing exact approval/journal rules |

The coding preset is offered only when the backend actually implements and
enables those capabilities for that root. It cannot widen the parent's policy.
An unavailable preset fails explicitly; a requested coding task is not silently
downgraded to investigation. Child recursion is initially disabled for both
presets. Broader presets and bounded recursive execution remain future native
work with the same root budget.

Deterministic tool nodes select only aliases in the root's current native tool
catalogue. For MCP, the backend derives the server ID/revision from the trusted
alias binding obtained by actual root discovery. A child still privately
resolves credentials and freshly rediscovers the exact alias before proposing
or dispatching an effect. Metadata, plan validation and plan admission do not
launch a peer. A changed connector or schema cannot silently rebind old work.

Human nodes require an implemented authenticated controller-input capability.
The model selects a bounded question, not its answering actor, route or
expiration policy. A human answer is dependency data. Even an answer saying
"proceed" cannot turn an effect into an approved operation; the later exact
file/process/MCP proposal still uses `request_operation`, `decide_operation`,
`claim_operation` and the real adapter outcome.

## Model-facing plan changes

Proposed tools are `plan_tasks` for initial creation and `revise_plan` for
changing the current plan. Their native closures supply the owning root and
plan identity. The model supplies revision/state preconditions and task data.
For example:

```json
{
  "expected_revision": 0,
  "expected_state_sequence": 0,
  "add": [
    {
      "id": "investigate",
      "type": "agent",
      "objective": "Read the failing parser path and identify the cause.",
      "preset": "workspace.inspect",
      "depends_on": []
    },
    {
      "id": "choose-behavior",
      "type": "human",
      "question": "Which of the two documented input behaviors is intended?",
      "depends_on": [{"task": "investigate", "require": "success"}]
    },
    {
      "id": "repair",
      "type": "agent",
      "objective": "Implement the chosen behavior and request review of the exact edit.",
      "preset": "workspace.coding",
      "depends_on": [{"task": "choose-behavior", "require": "success"}]
    }
  ]
}
```

This is a proposed payload, not a current execution receipt. Agent, tool and
human nodes have exclusive field sets. A tool node uses `tool` and a strict
`arguments_json` object string; human nodes use a bounded `question`. Arguments
and references remain data and cannot create capability fields. Unknown
fields, duplicate keys/labels, invalid UTF-8, cycles, undeclared references,
unsupported presets/tools and impossible resource requests reject before a
revision is accepted.

`revise_plan` accepts bounded `add`, `replace` and `skip` lists. The backend
constructs the next full candidate from its existing ledger; the model cannot
omit protected history by sending a replacement graph. `replace` and `skip`
apply only to never-claimed pending or blocked work. A skipped label is retired
permanently; adding another task requires a new label/backend node identity.
Claimed tasks, published human questions, settled outcomes and effect-linked
definitions retain their exact definition/dependencies/preset binding.

Dependencies distinguish `require: success` from `require: observed`. A real
failed read can be observed by a diagnostic task without pretending the read
succeeded. Effect uncertainty is never an ordinary observed-success edge.
Conditions, if enabled, compare bounded paths in owned dependency outputs and
must not add instructions or permissions. Each task's unresolved input is
retained separately from its exact resolved argument/prompt snapshot at claim.

Retain the existing 32 KiB per-node and 64 KiB joined encoded-result bounds,
including UTF-8 escaping and identity metadata. Full actual child history and
operation receipts remain durable. Oversize reports use explicit limit errors
and owned result references, preserving the actual node/run/effect states.
Add a native `inspect_plan` read tool bound to the same root for revision,
protected-node and bounded result inspection; it must not ask the model to
fetch an authenticated URL or copy a signed child continuation. Field/window
reads are schema-checked and bounded; they cannot modify state or make an
uncertain result successful.

Proposed initial bounds retain two concurrent executable children, eight
lifetime admitted children, 32 shared model attempts and depth one. Add a
backend limit of 32 lifetime plan labels, 16 accepted revisions, eight human
requests and 256 KiB per change request. These values must be a registered
policy, not user/model authority. All provider planning/replanning calls count
against the same root allowance. Limits can be changed by a later backend
policy revision for new roots, never by rewriting an admitted budget.

## Proposed v11 durable relations

These new relations are a schema proposal. Existing v10 delegation records and
registered graph journals remain intact; no historical plan/budget is invented.
Migration and trigger replacement remain one xlang3 SQLite transaction.

| Relation | Required identity/state |
| --- | --- |
| `dynamic_plans` | Backend plan ID, unique ordinary root ID, current definition revision, state sequence, lifecycle state, capability snapshot revision/hash and checkpoint |
| `dynamic_plan_revisions` | Immutable `(plan_id, revision)`, parent revision, full validated specification, accepted event sequence and exact originating plan-call ID |
| `dynamic_plan_calls` | Owning root/model-attempt identity, provider tool-call ID, exact arguments and actual signed assistant, accepted revision, observed result, continuation commit sequence and optional unique continuation-attempt binding; unique `(root_run_id, provider_tool_call_id)` |
| `dynamic_plan_nodes` | Immutable `(plan_id, revision, node_id)` definition/preset/binding records; backend node IDs and stable task labels |
| `dynamic_plan_edges` | Versioned declared dependency, `success`/`observed` requirement and validated condition/reference data |
| `dynamic_node_executions` | Stable plan/node identity, pending/blocked/claimed/settled/skipped/uncertain state, immutable claim-definition revision, optional unique owned child ID, resolved input, write-once outcome/event sequence |
| `dynamic_human_requests` | Backend request ID, exact plan/node/definition revision/question, waiting/answered/cancelled/expired state, answer/controller event, expiry and write-once authenticated answer |
| `agent_budget_segments` | Root/segment identity, measured active elapsed time, closed pause segment and remaining active wall allowance; no adoption of an unfinished live segment |

The admitted root capability/pause policy needs an immutable typed record too;
it cannot live only in a mutable `information` blob. New planning-enabled roots
use a proposed `native.dynamic-plan` revision 1 execution policy. Budget readers
continue accepting existing `native.delegation` revision 1 records. The new
policy can use the same model-attempt tables and provider identity rules while
binding the additional capabilities and pause accounting.

Add `planned_children_reserved` to the durable root budget. Revision acceptance
reserves capacity for its newly pending executable nodes. Dispatch converts a
reservation to `children_admitted` atomically. Skipping an unclaimed task frees
only its unused reservation; actual admitted children and model attempts are
never refunded. Existing `delegate_tasks` admissions must respect those plan
reservations and share the very same live budget object and concurrency slots.
Otherwise a later independent batch could consume capacity promised to a plan.

SQL boundaries must preserve:

- An ordinary dynamic child requires its exact accepted claim/definition,
  matching normal running parent/session and root budget. Task/claim data is
  written before the queued child under a deferred FK, mirroring v10 ownership.
- Claim identities, frozen definitions, resolved inputs and effect bindings
  cannot be rewritten. Child and node identities cannot be reused.
- Settlement is write-once and binds an event owned by that exact child, its
  actual terminal state and the same outcome payload. Human answers similarly
  bind their exact request and authenticated input event.
- A root cannot pause with active children/transport claims or executing
  operations. It cannot complete with pending work, human input, an uncommitted
  plan call or unresolved effects. Typed cancellation may retire pending work
  only after dispatched children drain and real outcomes settle.
- Generic `append_event` cannot create `plan.*` lifecycle evidence; those
  prefixes belong to typed transactions, as `delegation.*` and `budget.*` do now.

## Native transactions and proposed API surface

The signatures below retain the original proposed shapes rather than current
callable declarations. Initial v11 implements typed equivalents in the
[repository header](../Native/include/agentflow/repository.hpp) and
[persistence service](../Native/include/agentflow/persistence_service.hpp).
Repository methods remain confined to PersistenceService's worker; model/view
callers do not call them directly. The initial schema combines current node
definition and execution state in `dynamic_plan_nodes`, with immutable revision
copies, rather than adding the separate `dynamic_node_executions` relation in
the broader relational sketch above.

```cpp
DynamicPlanRecord accept_dynamic_plan_change(
    const std::string& owned_root, const DynamicPlanChangeSpec& actual_call);
DynamicFrontierClaim admit_dynamic_frontier(
    const std::string& plan, std::int64_t expected_revision,
    std::int64_t expected_state_sequence, std::int64_t expected_budget_revision,
    const std::vector<DynamicTaskClaimSpec>& backend_prepared);
DynamicNodeRecord settle_dynamic_child(const std::string& actual_child);
DynamicPlanStepResult settle_dynamic_plan_step(const std::string& plan_call);
DynamicHumanRequest input_dynamic_human(
    const std::string& plan, const std::string& request,
    const std::string& input_json, const std::string& authenticated_actor,
    std::int64_t expected_revision, std::int64_t expected_state_sequence);
Run suspend_dynamic_owner(const DynamicPauseSpec& measured_owner_segment);
DynamicResumeRecord resume_dynamic_owner(const DynamicResumeSpec& exact_owner);
void commit_dynamic_tool_turn(const std::string& owned_root,
    const std::string& plan_call); // Reads actual assistant/result from ledger.
```

`DynamicPlanChangeSpec` includes the exact provider call, raw arguments, actual
assistant, model-attempt correlation and expected revision/state sequence.
Backend prepared task claims carry only their validated frozen node/input
bindings and assigned IDs. The repository revalidates them against its accepted
revision and capability record; a successful topology parse is not admission
authority.

Revision acceptance performs one transaction:

1. Validate the owning running ordinary root, frozen policy, actual signed tool
   call, strict input and declared tasks. Validate pure derived fields before a
   duplicate-call return, as v10 does.
2. Deduplicate the exact call. Changed raw bytes, assistant or authority binding
   reject; an identical call reads the old ledger without accepting a revision
   or scheduling again. Interrupted calls are inspection-only.
3. Compare the expected plan revision and state sequence. Build the candidate
   from recorded work, reject changes to protected nodes, validate the DAG,
   references, tools/presets and remaining budget reservations.
4. Append the immutable revision, pending-node/edge records and capability
   bindings; update the head/checkpoint and planned-child counters by CAS.
   Hold one parent continuation allowance for the accepted call.
5. Append the typed accepted event and originating call record, then commit.
   Queue capacity is reserved before this transaction; physical dispatch begins
   only after its successful commit. Failure rolls back every row/counter/event.

Admission and replanning serialize on the plan head/state sequence. A frontier
claim atomically takes eligible nodes, their exact definition revision/resolved
inputs, root budget capacity and child IDs. A revision racing with that claim
either sees protected claimed work or rejects its stale precondition. It cannot
replace an effect while its worker is acquiring authority. Retry is permitted
only for undispatched metadata CAS refresh, never a model call or effect.

Settling a child derives actual history and journal state. It writes its outcome,
event, node transition and new state sequence together. The report and parent
tool-result commit are also typed transactions; failure leaves the call/owner
for recovery instead of inventing a conversation or success.

## Coordinator and scheduler

Add `DynamicPlanCoordinator` rather than weakening registered graph semantics.
Extract/share strict DAG, dependency-reference and bounded-output helpers from
GraphPlan/GraphCoordinator with their existing contracts unchanged. The dynamic
coordinator owns a separate checkpoint format and revision reducer.

Its execution states distinguish pending, blocked, claimed, waiting-human,
settled, skipped, cancelled and uncertain. An observed terminal child also has
its actual `child_state` and effect state. A failed child with a durable result
can be `settled` for observation while remaining failed; it is not a successful
GraphCoordinator completion. Success edges check actual success, observed
edges check a recorded non-uncertain outcome. An immutable graph projection
may validate a candidate's topology, but cannot decide dynamic success,
mutable skipping or replanning by itself.

The revision reducer carries all retained nodes' actual states/outputs forward.
Only never-claimed pending/blocked definitions can change; new nodes start
pending and retired labels stay retired. Claimed/settled dependencies and
definitions must match exactly. Recompute readiness from that ledger, never
from model-reported states or text containing "done". Missing dependency data
fails before approval/dispatch. Preserve raw `arguments_json` through storage
and reference substitution; MCP literals retain their tokens and escaped keys.
Existing conservative rejection of floating/unsafe dependency numbers remains
until a separately validated exact-output representation is added.

A common native child executor extends the current independent worker pool
for both actual AgentRunners and deterministic tool handlers. Agent and tool
nodes consume the same per-root child/concurrency allowance; human nodes have
no model child or executable worker. Separate root and child workers prevent
the occupied-parent deadlock already covered by delegation contracts. Coding
children use their advertised preset and the existing journalled effect
handlers, not a parallel unapproved filesystem/process implementation.

Extend `ChildAdmissionKind` with distinct dynamic agent/tool kinds and their
plan/node/claim revision identities. AgentRunner's current `delegated_leaf`
check deliberately forbids effects and bounds turns to four; it must retain
that meaning. A dynamic coding agent instead validates its exact registered
preset and shared root budget before acquiring a model claim. Likewise,
`reserve_model_call` must recognize that exact dynamic agent ownership rather
than accepting arbitrary children or creating another allowance. Deterministic
tool children make no provider reservation unless they actually invoke a model.

The owning scheduler processes ready frontiers until they settle, block or need
human input. At an observed failure with undispatched blocked work, it returns
a bounded report to the parent model. The next actual `revise_plan` call can
replace that pending verification task or add a new investigation; old failed
children remain immutable. This is a revision of the same plan, unlike v10's
separate independent follow-up batches.

Uncertain effect domains retain their current journal quarantine. The plan
records uncertainty, drains its workers and stops automatic continuation;
replanning cannot hide the node, move the operation to another owner, change a
connector revision to release contention, or relabel its dependency as success.
Explicit existing recovery/inspection is required. No blind retry occurs.

## Human pauses and signed continuation

A human pause is a new ordinary-Agent lifecycle, not currently supported by
AgentService/AgentRunner. Extend their owner state machine to queued, working,
waiting and faulted, following the existing GraphService pattern without using
its graph-only persistence methods. Waiting roots remain owned and occupy
admission capacity; provider/profile mutation still requires true idle state.

Initially require a planning tool to be the sole tool call in its assistant
turn. Reject a mixed planning/effect batch before any tool dispatch; document
this native protocol restriction. The accepted `dynamic_plan_calls` record
retains that actual signed assistant and exact call while its result is pending.
It is not appended as an unmatched public conversation row. Supporting arbitrary
mixed suspended turns later requires a durable owned tool-call ordinal/result
ledger, not keeping prior effects only in worker memory.

When only human work remains, the native owner drains executable children,
checks unresolved effects, persists exact requests/checkpoint/pending call and
closes its measured active budget segment in one transaction before pausing.
It releases the worker without invoking a provider. A registered pause policy
excludes this closed human-wait interval from active execution time, preserves
all child/model counters and holds the parent continuation allowance. Human
expiry is backend-owned and bounded; the model cannot extend it.

Publication, input scheduling and worker retirement share the owner's lock and
durable state checks. An answer arriving between pause publication and worker
retirement must not be lost: re-read the committed root/checkpoint before
entering waiting state, as GraphService does today. Input received while other
children run changes dependency state without enqueueing a second owner.

Authenticated input verifies exact root/session/request ownership, question and
revision/state preconditions. It records a write-once answer and typed event,
updates dependency state, and evaluates **owner readiness**, not only child
readiness. Queue the same owner when a child frontier is executable, when the
plan step can settle its report or commit its pending signed tool result, or
when typed expiry/failure/cancellation needs retirement progress. A final human
answer can make the report ready even though no child is ready to dispatch.
The owner then commits the same accepted planning call and uses its held parent
continuation allowance; it does not create another root, child or originating
provider request. Owner phase/queue checks prevent duplicate scheduling, and
the call's write-once commit marker prevents another result commit. Its held
continuation slot is durably bound to one native model reservation before
transport; repeat wakeups reuse that binding rather than minting another
attempt. The reservation's once-only start gate prevents repeated continuation
dispatch.

Check frozen capabilities and remaining execution budgets before dispatch or
provider continuation. Stale settings retain inspection/cancellation while
rejecting resume; expired execution allowance permits typed retirement, not a
fresh provider call. Human input is not a credential/configuration change or
effect approval.

Resume loads the recorded pending plan call and settled node histories, opens a
new segment with only the recorded remaining active wall allowance, and
continues undispatched work. It does not repeat the provider response which
created the plan or reopen retired children. Once the plan step has an actual
bounded report, `commit_dynamic_tool_turn` writes the stored signed assistant
and correlated result with its ledger commit marker atomically, then the parent
can make its reserved continuation call on the same provider wire.

A model terminal response while work remains cannot complete the root. The
native owner records an explicit incomplete-plan failure, drains active work
and retires only pending tasks through a typed cancellation transaction.
Completed work and uncertain effects remain visible. Native failure handling
must not recurse through a terminal guard while leaving an apparently healthy
owner indefinitely running.

## Cancellation, recovery and observation

User cancellation first stops scheduler admission, requests stop on all owned
jobs, cancels unanswered questions and drains actual workers. It records child
outcomes and retires pending nodes before the parent can become terminal.
Existing effect adapters retain succeeded/failed/uncertain outcomes. Recording
a budget retirement, settlement or signed conversation failure faults the
service and preserves claims; another provider call cannot paper over it.

Owner-lease recovery never adopts a live plan segment, started model attempt,
claimed child or executing effect. It records interrupted/failed/uncertain
states as appropriate and preserves completed rows/history. Only a fully
committed human pause with a closed budget segment, no active child/effect and
the exact frozen capability snapshot may be adopted for inspection and
authenticated resume. Partial acceptance/claim/input transactions roll back;
there is no child replay on cursor reconnect or service startup.

Proposed read interfaces are `GET /v1/runs/{root}/plan`, revision history and
human-request reads. Existing child/history/tree APIs extend their typed DTOs
with dynamic plan/node/claim revision identity. The existing `delegated_leaf`
and graph DTOs keep their meaning. Add a scoped human-input POST binding the
request plus expected revision/state sequence; actor comes from authentication.
There is no public spawn, arbitrary plan-state mutation or caller-supplied
outcome API. Model-origin revisions are accepted only through the native tool
loop and actual signed call ledger.

CLI and shared right sidebar show dependencies, definition revision, state
sequence, immutable claimed/completed work, blocked reasons, outstanding
questions, actual operations and per-child response metrics. Views render
committed events and fetch an owned snapshot when behind; they never mark a
node complete because a model says it completed. Reconnect preserves selected
run/revision and continues from committed event cursors without resubmission.
Coding children must be labelled by their actual preset/capabilities rather
than the current delegated-leaf renderer's fixed read-only label. Capability
metadata advertises dynamic planning only when the native owner and required
observation/controller interfaces are implemented; older clients must report
unsupported dynamic records explicitly instead of dropping children or effects.

## Concrete next acceptance and later work

The implementation must pass actual native lifecycle contracts, initially with
clearly labelled synthetic provider replies and real storage/tools/effects:

1. A parent selects a DAG with two real investigations, an authenticated human
   question and an authorized coding child. Dependencies prevent early child
   dispatch. Independent histories/receipts/metrics remain isolated. A human
   answer alone does not alter a file; a later exact approved child proposal
   produces real bytes and a succeeded operation under that child's owner.
2. A real failed verification yields a parent report; a fresh signed replanning
   call replaces only blocked never-claimed work and adds a new dependency.
   The same plan advances revision, old failed/completed children stay intact,
   and the revised verification actually runs. This is more than another
   independent `delegate_tasks` batch.
3. Revision/admission races, duplicate calls, stale state sequences, cycles,
   injected owners/endpoints/actors, unavailable presets, changed MCP bindings
   and edits to claimed/effect-linked nodes reject without dispatch. Raw MCP
   argument literals survive revision/checkpoint/reference round trips.
4. Budget contention covers ordinary delegation plus planned reservations,
   agent/tool frontier concurrency and child/model limits. A held parent call
   survives competing child calls; human pause/resume consumes only remaining
   active time and cannot reset counters or expiry.
5. Real revision/admission/event/human-answer SQL faults roll back exact
   heads/claims/runs/prompts/counters/events. Concurrent duplicate settlement
   writes one exact owned outcome/event. Parent continuation commit faults
   preserve signed ledger and completed effects while degrading admission.
6. Cancellation during queued work, provider streaming, human wait, approval
   wait and an effect drains ownership with actual journal outcomes. An
   uncertain effect blocks replanning/retry; no "observed" edge grants success.
7. Crash/reopen across accepted revision, child claim, completed effect,
   pending signed tool result and clean human pause preserves each ledger.
   Only the clean pause resumes after exact capability validation; all live
   claims are recovered without replay. Stale human answers cannot affect a
   newer question or another root.
8. HTTP/CLI/shared-controller reconnect crosses plan revisions and more than
   one event page, proves child/controller ownership, and renders durable
   protected/blocked/human states. Actual rendered webpage, installed VS Code,
   live-provider planning and exact hosted gates need separate evidence.
9. A real parent planning call selects a final human-only node and pauses with
   zero child runs. The authenticated answer settles the step, wakes the same
   owner without executable children, commits the exact original signed
   assistant/correlated result once, and makes exactly one held parent
   continuation call. Total native model reservations are two: origin plus
   continuation; the held count returns to zero and no extra child is admitted.
   Duplicate answer/reconnect cannot repeat that origin, result commit or
   continuation. An expiry/failure path likewise wakes typed retirement rather
   than leaving a root paused merely because no child frontier exists.

The initial agent/human implementation now includes v11 ownership/revision
transactions, the ordinary-Agent scheduler, pause/continuation protocol and
thin interfaces. Its local native contract includes a dependency DAG, actual
replanning after failure, a human check and an exact approved coding-child
effect. This scoped result does not establish acceptance for the whole target.

The original proposed production ownership split is retained for comparison:

| Files/boundary | Next change |
| --- | --- |
| New `Native/include/agentflow/dynamic_plan_records.hpp` | Immutable capability, revision/change, claim, human, pause and report DTOs |
| New `dynamic_plan.hpp` / `Native/src/dynamic_plan.cpp` | Strict plan-change validator and revision-aware coordinator |
| `Native/include/agentflow/graph.hpp` / `Native/src/graph.cpp` | Extract reusable pure DAG/reference/output helpers with registered graph behavior unchanged |
| Repository/PersistenceService headers and sources | v11 migration, CAS methods, reserved capacity, exact call/claim/settlement/input transactions and recovery |
| Root budget header/source and delegation records | Planned-child accounting, segment/pause policy, dynamic agent attempt ownership and common concurrency |
| New `dynamic_plan_executor.hpp` / `.cpp`, existing delegation executor | One bounded agent/tool scheduler, preset validation and observed frontier execution |
| AgentRunner/AgentService/ExecutionPlatform/profile runtime | Registered planning tools, pending signed-call continuation, waiting-owner/resume and truthful capability metadata |
| HTTP/CLI, shared client/controller/renderer | Owned plan and human interfaces, child effects, cursor-safe revision presentation and exact controller review |
| New native repository/engine/HTTP contracts | The DAG/replanning/human/coding-effect and failure/recovery acceptance above |

The [initial implementation guide](native-dynamic-plan.md) identifies the subset
now implemented and its verification limits. Broader agent/tool scheduling,
reference helpers and further node kinds in this table still require their own
implementation and acceptance; the design itself supplies no execution evidence.

Native skills, authority-preserving context compaction, registered-graph budget
integration, broader bounded recursion, outbound A2A peer/task ownership and
the complete coding/provider parity matrix remain required work. These use
the same durable observed-results and capability rules. Private Nexus team
coordination, PostgreSQL, WebRTC and Electron remain outside this OSS design.
