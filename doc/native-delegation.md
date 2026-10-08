# Native Agent delegation

Status: implemented source checkpoint with a passing local guarded gate:
71/71 native contracts, 103 extension contracts and 19 browser contracts.
[Recorded provenance](evidence/native-delegation-local-provenance.json) binds
these results to 45 frozen source hashes. Exact hosted validation,
live-provider delegation, rendered UI and installed VS Code acceptance for
this new checkpoint remain pending.

Ordinary **Agent** mode can offer `delegate_tasks` to the configured model. The
model selects bounded, independent workspace investigations. Each accepted task
creates a real native AgentRunner with its own conversation and provider calls.
The parent receives their observed results, calls its model again, and can
request its own authorized coding actions or a new investigation after a child
failure. The user does not import a graph to use this path.

This is the initial local delegation cut in
[native-delegation-design.md](native-delegation-design.md). It does not complete
the broader [dynamic Agent target](dynamic-agent-execution.md), mutable planning
or the full coding-product goal.

## Model tool and registered policy

The native product server enables delegation for a healthy, configured Agent
generation with supported model tools and a verified workspace. Public health
reports the actual `agent_delegation` capability. Saved provider profiles and
the explicit-model server path use the same implementation. Embedded C++ users
must explicitly select `AgentDelegationPolicy`. AgentService creates its owned
executor; bare AgentRunner callers supply one. Library defaults preserve
existing non-delegating callers.

`delegate_tasks` is appended after the parent's existing workspace, coding,
process and MCP tool definitions. Its arguments have this shape:

```json
{
  "tasks": [
    {
      "id": "inspect-parser",
      "objective": "Read the parser and explain the failing input path.",
      "preset": "workspace.inspect"
    },
    {
      "id": "inspect-tests",
      "objective": "Read the tests and identify the expected behavior.",
      "preset": "workspace.inspect"
    }
  ]
}
```

The native boundary rejects duplicate JSON fields, unknown fields, malformed
UTF-8, duplicate task labels, empty objectives and arguments above 65,536 bytes.
Labels use 1–32 characters from `A–Z`, `a–z`, `0–9`, `_`, `.` and `-`;
objectives contain at most 8,192 UTF-8 bytes. The backend assigns batch, child
run and node identities. Model arguments cannot choose another parent,
workspace, provider, credential, controller, command or child outcome.

`workspace.inspect` revision 1 is an explicit read-only leaf preset. It offers
exactly `read_file`, `list_files`, `search_files` and
`read_repository_instructions`. Leaves cannot edit or create files, launch
processes, use MCP, select another model or delegate again. Unknown presets and
revisions reject explicitly. A model response requesting an unoffered tool
fails before a leaf effect or nested child is admitted.

The parent retains its configured coding tools and their existing exact
controller approvals and effect journals. Leaf results are data; they do not
grant an approval or change native authority.

## Execution and limits

| Bound | Product default |
| --- | --- |
| Tasks per call | 1–4 |
| Simultaneous leaves per root | 2 |
| Children admitted over the whole root | 8 |
| Delegation depth | 1 |
| Model-call attempts across parent and leaves | 32 |
| Model turns per leaf | At most 4, also bounded by the shared allowance |
| Encoded child result given to the parent | 32 KiB |
| Encoded joined tool result | 64 KiB |
| Shared root wall limit | The configured run timeout; default 10 minutes |

The wall clock starts in the owning root runner. A shared steady deadline and
stop token cover the parent and its leaves; a leaf cannot acquire a fresh root
deadline. Provider transport retains its own applicable timeouts. Cancellation
or deadline expiry is checked before a queued leaf constructs its runner and
before model invocation. Accepted stopped jobs retire through their actual
queued cancellation path.

One DelegationExecutor has four leaf workers and at most 64 pending jobs,
separate from AgentService's root workers. Root workers can wait for their
children without occupying the leaf pool. The scheduler acquires the shared
root's two-leaf limit before dispatch and releases it after actual execution.
These worker and queue bounds apply to an AgentService generation. The product
provider runtime owns one active execution generation and requires it to be
idle before profile replacement; arbitrary embedded services do not share a
process-wide pool.

Each leaf copies the parent's frozen provider/model configuration, credential
reference, verified workspace and backend instruction policy. It privately
resolves the existing provider credential, verifies the admitted public
provider and workspace identities, and loads applicable repository guidance
through the native reader. Its private user prompt contains exactly its
accepted objective and, when applicable, public provider-context metadata.
It does not receive the parent's transcript, sibling messages or another
execution's provider-native continuation receipts.

## Durable ownership and budgets

Repository schema v10 adds `agent_execution_budgets`,
`agent_model_call_reservations`, `delegation_batches` and `delegation_tasks`.
The existing local SQLite adapter runs through xlang3. Migration is
transactional; historical runs do not receive invented budgets or execution
timestamps.

A normal root's prompt, run, task-history ownership and immutable
`native.delegation` revision 1 budget are admitted together. The public budget
identity contains the existing six provider-profile fields, or only `wire` and
`model_id` for an unprofiled generation. Endpoints and secrets are excluded.
Shared counters never reset for another leaf, batch or follow-up request.

Every native model attempt reserves one allowance before invocation, acquires
its owned start transition, and retires that same attempt afterwards. A
reservation consumes its allowance even if cancellation stops it before
dispatch. Retrying an already started or finished attempt cannot authorize a
second invocation. Typed `budget.model_call.reserved`, `.started` and `.finished`
events record the actual attempt, owner and role; the finished event also
records whether the start transition occurred. No token totals are inferred
from these counters.

Each accepted delegation batch holds one parent continuation allowance.
Leaves cannot consume held parent allowances when racing for the final shared
model call. The parent consumes its held allowance on its next native call.
Child-count and shared-call limits apply before new batch admission.

Batch admission atomically records the actual parent assistant/tool call,
exact argument string, frozen preset, task rows, child runs, independent user
messages, history ownership, budget changes and events. The task row precedes
its child insertion under a deferred foreign key. The SQL child boundary
requires that exact accepted task under its running normal root; the existing
registered-graph branch is retained separately.

`(parent_run_id, provider_tool_call_id)` deduplicates admission. Exact repeated
calls observe the existing immutable batch without scheduling children again.
Changed argument bytes, assistant turn or preset reject. Pure task/objective,
prompt and provider validation also precedes this replay return. Newly
generated backend IDs do not replace the previously admitted children.

Child settlement derives the outcome from its actual terminal run and history.
The outcome and `delegation.child.settled` event commit together. A write-once
SQL boundary requires an event owned by that exact child with the same payload
and terminal state. Duplicate settlement returns the same committed outcome
and sequence. Batch settlement derives and journals its join once all child
outcomes are recorded. The parent cannot pause or retire around active children
or an unsettled accepted batch.

## Joined results and conversation fidelity

A completed child supplies only its observed content, model, available usage
and measured timing to the join. Signed/native provider continuation items
remain inside that child's independent history. Missing usage fields remain
missing; the native code does not synthesize totals or combine siblings'
counters.

The joined tool result has this structure:

```json
{
  "batch_id": "backend-assigned-id",
  "source": "native_delegation",
  "children": [
    {
      "child_run_id": "backend-assigned-child-id",
      "task_id": "inspect-parser",
      "child_state": "completed",
      "preset_id": "workspace.inspect",
      "preset_revision": 1,
      "content": "Observed finding from this child's real execution"
    }
  ]
}
```

This example shows the shape, not an execution receipt. Actual envelopes may
include `model`, `usage`, `elapsed_ms`, `first_token_ms`, `error` and
`history_ref`. A failed read-only child is an observed result the parent can
use for a new investigation; it is not automatically rerun. Batch states
distinguish completed, failed and cancelled children.

Limits measure the actual UTF-8 JSON envelope including escaping and metadata.
An oversized child retains its full durable answer and actual run state, but
its parent envelope returns `result_limit_exceeded` with identity and a history
reference. If the entire join exceeds 64 KiB, a bounded identity/state/history
envelope reports the same explicit error. There is no silent truncation,
invented success or child replay.

The accepted ledger preserves the parent's actual signed tool-call turn. The
replayable parent conversation commits that assistant and all correlated tool
results together after the join. Partial or cancelled batches cannot leave
unmatched assistant/tool rows. Leaf signed histories are not copied into this
parent continuation.

## Cancellation, faults and restart

Parent cancellation stops queued and active leaf jobs. The executor drains all
admitted futures and records observed child and batch outcomes before ordinary
parent retirement. Existing parent edit, create, process and MCP adapters keep
their own approval, cancellation and uncertainty behavior.

If model-attempt retirement, child settlement, batch settlement or the coupled
parent conversation cannot be journaled, typed unrecorded-outcome exceptions
stop the service. Earlier completed child histories remain intact and the
parent claim stays available for recovery; the engine does not manufacture a
terminal success or try the call again. Faulted service admission closes, and
queued root jobs receive stop requests before provider/tool execution.

Owner-lease recovery marks unfinished runs failed, unfinished batches and
model reservations interrupted, and retains completed child results and
histories. Existing executing parent effects retain their journal's uncertain
recovery state. Recovery never relaunches children or adopts interrupted model
attempts. A later user investigation has its own new run and batch identities.

## Thin observation interfaces

| Native read interface | Meaning |
| --- | --- |
| `GET /v1/health` | Actual `agent_delegation` availability and owned-child observation support |
| `GET /v1/agent/delegation` | Enabled registered preset, tools and product limits |
| `GET /v1/runs/{parent}/children` | Owned run plus kind, batch/task and preset identity |
| `GET /v1/runs/{parent}/children/{child}/history` | History of a child belonging to that parent |
| `GET /v1/runs/{parent}/tree-events?after={seq}` | Committed parent/child events, at most 256 per batch |

Normal Agent delegation children use `kind: "delegated_leaf"`; generic reads
also return `graph_agent` and `graph_tool` for registered graph roots. Histories
and events enforce parent and session ownership. Existing graph endpoints continue to serve registered
graphs. These interfaces are reads under existing authentication and view
authorization; they do not expose a client-controlled spawn, settlement or
approval shortcut.

CLI commands `delegation`, `children ROOT`, `child-history ROOT CHILD` and
`tree-events ROOT [CURSOR]` observe the same native records. Agent `/watch` uses
tree observation when the backend advertises support. The webpage and VS Code
share the sidebar renderer's delegated-investigation cards, identified child
events, independent history and per-response metrics. Their controllers poll
committed cursors, verify child ownership, reconnect from durable state and
continue to send parent approvals through the existing review interface.
The clients do not own the scheduler or infer completed work from stream text.

## Validation and remaining scope

The final complete local guarded gate passed **71/71** native contracts in
**142.36 seconds**, with zero failures, skips or post-build exclusions. The
expected, registered and passing contract manifests match. The
[provenance record](evidence/native-delegation-local-provenance.json) includes
45 frozen source hashes, compiled binary hashes, the VSIX hash and all six
evidence-log hashes.

| Local validation | Recorded result |
| --- | --- |
| [Complete native gate](evidence/native-delegation-local-ctest.log) | 71/71; 142.36 s |
| `native_delegation_contract` in that gate | Passed; 1.96 s |
| `native_delegation_http_contract` in that gate | Passed; 1.03 s |
| [Extension contracts](evidence/native-delegation-local-extension.log) | 103 passed; 1.7564936 s |
| [Browser contracts](evidence/native-delegation-local-browser.log) | 19 passed; 0.894282 s |
| [Browser/native integration](evidence/native-delegation-local-browser-native.log) | Passed with matching sources, rebuilt native backend and shared assets |
| [Packaged VSIX verification](evidence/native-delegation-local-package.log) | Passed; all 18 required assets verified |

The [initial gate log](evidence/native-delegation-initial-ctest.log) retains its
70/71 result and the process peer's obsolete positional schema assertion after
the delegation tool was appended. The fixture now locates `run_process` by its
exact name while retaining full ordered-catalogue and exact-profile assertions.
The corrected final source passed the whole gate; no failed contract was
excluded or relabelled as a passing initial attempt.

`native_delegation_contract` drives the native engines against labelled
synthetic provider protocol replies while using actual filesystem reads,
controller-approved parent edits, xlang3 SQLite, cancellation and storage
faults. It also covers follow-up investigations, two occupied root workers,
held continuation races, atomic admission, exact deduplication, settlement and
v9 migration/recovery. `native_delegation_http_contract` exercises the actual
server, CLI, thin controllers and approval/effect path with a synthetic local
provider. These passing controlled contracts establish the local implementation
boundary; their supplied provider replies, signatures and keys are fixtures.

Exact hosted validation and live-provider delegation remain pending. The
adapter/DOM checks and packaged assets do not establish rendered webpage or
installed VS Code acceptance for v10. The earlier c4 installed preview remains
unchanged; upgrade and installed-product acceptance are separate work.

Full planning still requires model-selected dependency DAGs, human checks,
accepted plan revisions and mutations of undispatched work after observations.
Delegation inside registered graph agent nodes, broader leaf presets with
approved effects, context compaction, native skills and outbound A2A delegation
remain separate requirements. Private Nexus team-server capabilities remain
outside this OSS checkpoint. Evidence for live providers, installed extension
use and rendered webpage behavior must be reported separately when exercised.
