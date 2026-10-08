# Native dynamic delegation design

Status: the initial ordinary-Agent, read-only leaf delegation path is
implemented and passed the complete local 71/71 native gate, 103 extension
contracts and 19 browser contracts. The
[source-bound evidence](evidence/native-delegation-local-provenance.json) records
the exact local scope; hosted, live-provider, rendered UI and installed-product
acceptance for this new checkpoint remain pending. Its concrete behavior,
durable v10 ownership/budgets and thin observation interfaces are documented in
[native-delegation.md](native-delegation.md). This design remains the broader
target: model-selected dependency graphs, human checks, revisioned replanning,
skills, context compaction and outbound A2A are not completed by that initial
cut. Earlier registered graph and direct MCP graph evidence remain separate
from this delegation checkpoint.

Normal **Agent** mode should let the model delegate concrete investigations,
observe their actual results, and continue coding through the same native
execution and approval engine. A user should not have to import a graph for
each coding task. The full target in [dynamic-agent-execution.md](dynamic-agent-execution.md)
still includes model-selected dependencies, human checks, revisioned replanning
and remote delegation. Delivering bounded local leaf agents establishes one
part of that target; it does not redefine the goal or establish full planning
or OpenCode parity.

## First product behavior

Register a native `delegate_tasks` tool with the parent AgentRunner. Its initial
tasks are independent leaf investigations followed by a join. A real child
AgentRunner calls the configured provider, selects its registered read tools,
and returns an observed outcome. The parent receives those outcomes as the
result of its actual delegation tool call and invokes its model again. It can
then request an approved edit, run an already registered verification command,
or ask for a different investigation after a failed child.

The initial tool accepts only this bounded shape:

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

Reject duplicate keys, unknown fields, duplicate task IDs, malformed UTF-8,
empty objectives and input above 65,536 bytes before admission. Task labels
contain at most 32 identifier characters; objectives contain at most 8,192
UTF-8 bytes. Labels identify tasks inside a batch, not runs, actors or execution
authorities. The backend assigns batch IDs, child run IDs and unique `node_id`
values. Model arguments cannot supply commands, endpoints, credentials,
workspaces, provider profiles, settings revisions, approval decisions or child
outcomes.

The proposed initial registered limits are:

| Bound | Initial value |
| --- | --- |
| Tasks in one delegation call | 1–4 |
| Simultaneous children in one root execution | 2 |
| Total children admitted by one root | 8 |
| Delegation depth | 1; leaves cannot delegate |
| Model calls across parent and children | 32 |
| Model turns in one leaf | At most 4, also limited by the shared budget |
| Child result supplied to the parent | At most 32 KiB per encoded child envelope |
| Final delegation tool-result envelope | At most 64 KiB |

These are explicit backend policy limits, not inferred provider capabilities or
token usage. The first admission path is a normal Agent root. Delegation from
an agent inside a registered graph requires the later generic ownership/tree
integration to use that graph root's same budget; it must not create another
independent root budget or masquerade as a registered graph.

## Registered leaf authority

The proposed `workspace.inspect` is an explicit, revisioned native execution preset. Its
advertised tool set is `read_file`, `list_files`, `search_files` and
`read_repository_instructions` in the parent's already verified workspace.
The preset disables leaf delegation, file mutations, process execution and MCP
effects. Its name, revision and enabled tools appear in backend metadata,
admission events and the child record. The tool description states that these
children perform read-only investigations.

This restriction belongs to the selected leaf preset. It does not silently
downgrade the user's general parent agent: the parent retains its configured
coding tools and their existing approval requirements. Requests for an unknown
or more capable preset fail explicitly instead of falling back to inspection.

Each child inherits the parent's immutable provider/model generation, verified
workspace identity and backend instruction policy. It resolves the existing
provider credential privately. It receives its own bounded objective/context
and independently loads applicable repository instructions through the native
workspace reader. It does not receive another child's conversation, the whole
parent transcript, provider keys or provider-native continuation receipts from
a different execution.

Later registered coding presets may enable a subset of the parent's mutation,
process or MCP tools. Those effects must still use the existing exact
controller approval, workspace/resource claims, instruction preconditions and
operation journal under the actual child run. Delegation itself never grants
an effect, bypasses quarantine, changes the authenticated actor or transfers
ownership to a view. Shared-workspace effects retain the current serialization
and uncertainty rules.

Every task in the initial batch must select the same enabled
`workspace.inspect` revision; validate this before admission. The proposed
batch-wide preset binding below is valid for that restriction. Before enabling
mixed presets, move the immutable binding into each task and extend its
admission/settlement contracts; do not reinterpret an old batch-wide binding.

## Native ownership and execution

AgentRunner registers the tool only when the backend has an enabled delegation
policy and an eligible root. It validates the requested preset and limits,
then sends a typed admission request to PersistenceService. Repository owns
the transaction; views and model output cannot insert executable children.

The delegation owner schedules the accepted children through real AgentRunner
instances. It uses a bounded leaf executor separate from AgentService's root
workers. Enqueuing children behind occupied parent workers would deadlock when
every root worker waits for its children. Allocate future/ownership tracking
before launching a worker, retain the stop source for every launched child,
and join every owned worker on all return and exception paths.

The backend also needs a global leaf-worker/queue ceiling across roots; the
initial proposed pool has four workers and at most 64 waiting leaf jobs, while
each root can occupy at most two workers. Reserve queue capacity before durable
admission, or return an explicit capacity rejection without creating children.
These backend-wide limits complement each RootExecutionBudget instead of
allowing every concurrent root to create an unbounded independent pool.

The parent remains running while waiting for the batch. Completed children
retain their independent history, raw provider continuation receipts, supplied
usage, measured timing and preset/provider identity. The bounded join contains
child IDs, observed status and result text, with actual optional metrics. It
does not copy child signed receipts into the parent's provider history or
invent a model response, aggregate token total or success for a failed child.
Failed read-only children can return typed failure results that the parent
uses to decide its next action.

If a child's actual response or the combined join exceeds its envelope bound,
return an explicit limit outcome with the affected child IDs and retained
history references. Preserve the completed child/history and original metrics;
do not silently truncate its answer, synthesize a shorter successful response,
or rerun it to fit the join.
Measure these limits on the actual UTF-8 serialized JSON envelopes, including
escaped text, child identities, failure details and metrics, before appending
parent completion events or committing a conversation result.

## Proposed SQLite v10 relations

The current repository is schema v9. The next migration must retain existing
sessions, runs, messages, task history, graph checkpoints, operations,
credentials and status clocks. All statements still run through embedded
xlang3. The following is proposed schema, not executed SQL:

```sql
CREATE TABLE agent_execution_budgets(
  root_run_id TEXT PRIMARY KEY NOT NULL REFERENCES runs(id),
  policy_id TEXT NOT NULL,
  policy_revision INTEGER NOT NULL CHECK(policy_revision > 0),
  workspace_identity TEXT NOT NULL,
  provider_identity_json TEXT NOT NULL
    CHECK(json_valid(provider_identity_json)
      AND json_type(provider_identity_json) = 'object'),
  max_children INTEGER NOT NULL CHECK(max_children BETWEEN 1 AND 8),
  max_parallel INTEGER NOT NULL CHECK(max_parallel BETWEEN 1 AND 2),
  max_model_calls INTEGER NOT NULL CHECK(max_model_calls BETWEEN 1 AND 32),
  wall_limit_ms INTEGER NOT NULL CHECK(wall_limit_ms BETWEEN 1 AND 3600000),
  children_admitted INTEGER NOT NULL DEFAULT 0
    CHECK(children_admitted BETWEEN 0 AND max_children),
  model_calls_reserved INTEGER NOT NULL DEFAULT 0
    CHECK(model_calls_reserved BETWEEN 0 AND max_model_calls),
  parent_calls_held INTEGER NOT NULL DEFAULT 0 CHECK(parent_calls_held >= 0),
  revision INTEGER NOT NULL DEFAULT 1 CHECK(revision > 0),
  CHECK(model_calls_reserved + parent_calls_held <= max_model_calls)
);

CREATE TABLE agent_model_call_reservations(
  root_run_id TEXT NOT NULL REFERENCES agent_execution_budgets(root_run_id),
  attempt_id TEXT NOT NULL,
  owner_run_id TEXT NOT NULL REFERENCES runs(id),
  role TEXT NOT NULL CHECK(role IN ('parent','leaf')),
  state TEXT NOT NULL CHECK(state IN ('reserved','started','finished')),
  PRIMARY KEY(root_run_id, attempt_id)
);

CREATE TABLE delegation_batches(
  id TEXT PRIMARY KEY NOT NULL,
  parent_run_id TEXT NOT NULL REFERENCES runs(id),
  root_run_id TEXT NOT NULL REFERENCES agent_execution_budgets(root_run_id),
  provider_tool_call_id TEXT NOT NULL,
  arguments_json TEXT NOT NULL
    CHECK(json_valid(arguments_json) AND json_type(arguments_json) = 'object'),
  parent_assistant_json TEXT NOT NULL
    CHECK(json_valid(parent_assistant_json)
      AND json_type(parent_assistant_json) = 'object'),
  preset_id TEXT NOT NULL,
  preset_revision INTEGER NOT NULL CHECK(preset_revision > 0),
  state TEXT NOT NULL CHECK(state IN
    ('accepted','working','completed','failed','cancelled','interrupted')),
  result_json TEXT
    CHECK(result_json IS NULL OR
      (json_valid(result_json) AND json_type(result_json) = 'object')),
  UNIQUE(parent_run_id, provider_tool_call_id)
);

CREATE TABLE delegation_tasks(
  batch_id TEXT NOT NULL REFERENCES delegation_batches(id),
  task_id TEXT NOT NULL,
  node_id TEXT NOT NULL,
  child_run_id TEXT NOT NULL UNIQUE REFERENCES runs(id)
    DEFERRABLE INITIALLY DEFERRED,
  objective TEXT NOT NULL,
  outcome_json TEXT
    CHECK(outcome_json IS NULL OR
      (json_valid(outcome_json) AND json_type(outcome_json) = 'object')),
  settled_event_seq INTEGER REFERENCES events(seq),
  CHECK((outcome_json IS NULL AND settled_event_seq IS NULL) OR
    (outcome_json IS NOT NULL AND settled_event_seq IS NOT NULL)),
  PRIMARY KEY(batch_id, task_id),
  UNIQUE(batch_id, node_id)
);
```

`runs.state` remains the authority for child lifecycle. Task outcome fields
record only an observed terminal outcome; they are not a second client-mutable
run state. Provider identity stores public immutable profile/route/wire/model
identity, not secrets or destinations. The steady-clock deadline stays in its
owning native execution object; a persisted wall limit is configuration and
audit data, not permission to resume an interrupted clock after restart.

Create the enabled root's budget row in the same transaction as its ordinary
prompt/incoming-message run admission, before its first model call. Repository
receives a typed budget specification from the trusted execution generation,
not HTTP/model arguments. Existing completed/legacy runs do not acquire
fictional historical reservations or usage through migration. Construct the
live shared budget when the owning worker claims that queued root.

Migration must replace the v5 `graph_child_boundary` trigger with a strict
owned-child boundary. Preserve its existing graph-parent branch. A new
delegation branch must require an accepted task with this exact child ID and
node ID, its matching batch parent, the same session and a running parent.
For the first milestone that parent must be a normal root, not a graph root or
another child. Do not merely allow arbitrary children beneath any running
agent. The deferred child foreign key allows the task and run to be inserted
together; both must exist when the transaction commits.

The proposed trigger makes that boundary concrete:

```sql
DROP TRIGGER graph_child_boundary;
CREATE TRIGGER owned_child_boundary BEFORE INSERT ON runs
WHEN NEW.parent_run_id IS NOT NULL
BEGIN
  SELECT CASE WHEN NEW.node_id IS NULL OR NOT (
    EXISTS (
      SELECT 1 FROM runs p JOIN graph_roots g ON g.run_id = p.id
      WHERE p.id = NEW.parent_run_id AND p.parent_run_id IS NULL
        AND p.session_id = NEW.session_id AND p.state = 'running'
    )
    OR EXISTS (
      SELECT 1 FROM delegation_tasks t
      JOIN delegation_batches b ON b.id = t.batch_id
      JOIN agent_execution_budgets e ON e.root_run_id = b.root_run_id
      JOIN runs p ON p.id = b.parent_run_id
      WHERE t.child_run_id = NEW.id AND t.node_id = NEW.node_id
        AND b.parent_run_id = NEW.parent_run_id
        AND b.root_run_id = b.parent_run_id
        AND b.state IN ('accepted','working')
        AND p.parent_run_id IS NULL AND p.state = 'running'
        AND p.session_id = NEW.session_id AND NEW.state = 'queued'
        AND NOT EXISTS (SELECT 1 FROM graph_roots g WHERE g.run_id = p.id)
    )
  ) THEN RAISE(ABORT, 'invalid owned child boundary') END;
END;
```

The graph branch retains the existing trigger boundary; graph-node declaration
checks still belong to the typed graph admission transaction. The delegation
branch adds no public authority to write batches/tasks. The native admission
transaction validates the preset, objective and counters before inserting the
rows this trigger observes.

Settlement must also bind its event to the exact child and be write-once. The
following proposed triggers prevent pre-settled admission, changing an accepted
task identity, or attaching an unrelated event to its observed outcome:

```sql
CREATE TRIGGER delegation_task_initial_outcome BEFORE INSERT ON delegation_tasks
WHEN NEW.outcome_json IS NOT NULL OR NEW.settled_event_seq IS NOT NULL
BEGIN
  SELECT RAISE(ABORT, 'new delegation task must be unsettled');
END;

CREATE TRIGGER delegation_task_identity_immutable
BEFORE UPDATE OF batch_id, task_id, node_id, child_run_id, objective
ON delegation_tasks
BEGIN
  SELECT RAISE(ABORT, 'accepted delegation task identity is immutable');
END;

CREATE TRIGGER delegation_task_settlement_boundary
BEFORE UPDATE OF outcome_json, settled_event_seq ON delegation_tasks
BEGIN
  SELECT CASE WHEN OLD.outcome_json IS NOT NULL
    OR OLD.settled_event_seq IS NOT NULL OR NOT EXISTS (
      SELECT 1 FROM events e JOIN runs c ON c.id = OLD.child_run_id
      WHERE e.seq = NEW.settled_event_seq AND e.run_id = c.id
        AND e.kind = 'delegation.child.settled' AND e.payload = NEW.outcome_json
        AND c.state IN ('completed','failed','cancelled')
        AND json_extract(NEW.outcome_json, '$.child_run_id') = c.id
        AND json_extract(NEW.outcome_json, '$.child_state') = c.state
    ) THEN RAISE(ABORT, 'invalid delegation settlement boundary') END;
END;
```

The typed repository settlement method reads the actual terminal run/history,
constructs the bounded outcome, appends that child's settlement event, and
sets the two outcome columns together in one transaction. Concurrent identical
settlement requests return the already committed outcome/event without another
write; mismatches reject. Event insertion or outcome-update failure rolls back
both settlement fields and its event. It does not erase an earlier real child
completion. Batch settlement derives its result from these owned outcomes and
commits its status/result/parent event atomically once every child has retired.

Admission checks the current root budget revision and limits, inserts the
batch/tasks/queued child runs, writes each private child prompt and
`task_history_owners`/`task_messages` link, increments the admitted-child count,
and appends parent/child admission events in one transaction. A failure in any
statement or event rolls back the entire admission. The existing active-root
index already excludes children, and the existing `(parent_run_id,node_id)`
unique index continues protecting child identity.

The batch deduplication key is the provider's actual tool-call ID within its
parent run. An identical retry returns the accepted batch and its observed
result; it never creates replacement children. Changed arguments, assistant
turn identity or preset binding for that key reject. Native validation also
checks exact permitted tool-call ownership; SQL `json_valid` alone does not
establish duplicate-key, signed-receipt or authority validity.
An in-progress duplicate can only observe/wait on its existing owner; it cannot
launch another scheduler. After restart, interrupted batches remain inspection
records and cannot resume through this deduplication path.

## RootExecutionBudget

Introduce one native `RootExecutionBudget` shared by the parent and every
child. It carries the root's steady-clock deadline, cancellation state,
admitted-child allowance, model-call reservations and concurrency capacity.
Claim a model-call reservation before invoking provider transport; a rejected
or interrupted call still consumes its attempt. Reserve enough budget for a
parent continuation before granting a child batch. Capacity must apply across
all batches of the root, not reset for each tool invocation.

That continuation is a held allowance, not a comment in the tool prompt.
Batch admission increments `parent_calls_held` by one before allocating leaf
allowances. A leaf reservation can increment `model_calls_reserved` only if
that increment plus every held parent call still fits the shared limit. A
parent reservation consumes its held allowance and increments the reserved
count in the same transaction. A subsequent batch must establish another held
continuation before admission. Two leaf workers racing for the last unheld
slot cannot consume that parent reservation.

Each native transport attempt has one backend-generated `attempt_id`, bound to
its actual parent/leaf owner and root. Insert its reservation and increment the
budget with the expected revision in one transaction. Transition that exact
reservation to `started` once before invoking transport; `started` records
native invocation ownership, not proof that bytes reached the provider. A
duplicate attempt cannot invoke transport again. The initial implementation
adds no automatic provider retry. Any later allowed transport retry needs a
new reservation/attempt and must respect the same deadline, held allowance and
call ceiling; one logical call cannot conceal multiple unbudgeted requests.

Child deadlines and turn limits are intersections with the remaining root
budget. Their timers, schema workers, permission waits and transports receive
the shared deadline/stop state. Never start a child with a fresh full timeout
after the parent has spent part of its allowance. Future mutation-enabled
leaves retain the effect adapters' separately bounded acknowledgement
validation/journaling path after an observed effect.

Persist budget reservations/counts with revision checks for durable admission
and audit. Enforce live concurrency in the native owner, with repository run
claims as the durable ownership boundary. Exhaustion produces an explicit
recorded failure, not a fabricated completion. Supplied per-response usage
remains distinct from these execution limits; missing token counts remain
missing, and partial provider counters must not become invented totals.

## Conversation and failure boundaries

The accepted batch retains the actual parent assistant turn and exact requested
tool-call arguments before child launch. Its provider-native signed content
stays attached to that parent turn. This is execution-ledger material, not an
incomplete public assistant/tool conversation inserted ahead of its results.

After all owned children retire and the batch outcome is recorded, the parent
commits its assistant tool-call turn and every correlated tool result through
the existing atomic `record_tool_turn` boundary. A response containing several
tool calls remains one coupled conversation batch. Delegation must not split a
signed parent receipt, invent a tool-call ID, substitute another child's
provider receipt or leave dangling assistant calls in replayable history.

Generalize AgentRunner's graph-only child validation to a typed repository
admission check: a child is either a declared graph agent or an accepted
delegated leaf with its recorded preset. Generalize the repository parent
boundary so ordinary agent parents cannot complete, pause or retire while
their children are queued/running/paused. Child failures do not fabricate a
parent completion; the parent observes them and may continue within its budget.
Uncertain effects and unrecorded outcomes stop normal continuation through the
existing quarantine/fail-stop path.

Parent cancellation stops new admission, requests cancellation of every child,
retires demonstrably undispatched children, and joins dispatched workers before
parent retirement. Cancellation during an actual mutation must preserve the
adapter's observed outcome or uncertainty rather than claim the effect was
undone. Outcome-storage failures propagate after draining owners; they must not
be converted to generic failures that lose an executing claim.

AgentService currently stop-requests every active job, including queued jobs,
on failure/shutdown. It may dequeue those jobs while draining, but AgentRunner
checks their stopped token before entering running state or provider/tool
execution and retires them as cancelled. This is not evidence of physical
effect dispatch after a fault. Delegation must retain that distinction: check
stop/deadline before a constructor or worker launch can acquire a child process,
drain queued ownership without execution, and join dispatched owners. The leaf
scheduler may explicitly stop dequeuing on fault, provided it still retires
demonstrably undispatched jobs and preserves unfinished claims for recovery.

Owner recovery already fails queued/running executions and quarantines
unfinished operation claims. Extend its transaction to mark interrupted
delegation batches and preserve completed child results/history. Do not adopt,
relaunch or replay delegated children after restart. A completed batch whose
parent conversation failed to commit remains inspectable ledger evidence, not
authorization to execute it again or invent the missing parent turn.

## Repository and observation APIs

Add typed PersistenceService/Repository contracts for accepting a delegation
batch, claiming its admitted child, reserving model-call budget, settling an
observed child, reading batch metadata and listing generic owned children.
They remain backend-internal mutation APIs. Existing graph-specific contracts
retain their graph/spec/checkpoint validation.

| Native boundary | Required implementation change |
| --- | --- |
| `AgentSettings` | Immutable registered delegation policy/presets and startup limits |
| `AgentRunner` | Tool registration/dispatch, shared model-call reservations and typed child admission validation |
| New delegation executor | Bounded leaf scheduling, stop/deadline propagation, observed joins and unconditional draining |
| `Repository` / `PersistenceService` | v10 migration, atomic deduplication/admission/budget/outcome contracts and generic child ownership |
| `ExecutionPlatform` / services | Root-owned cancellation, fault health, queued cancellation drain and idle only after child owners retire |
| HTTP/CLI/shared adapters | Authenticated tree observation, private child histories, retained metrics and cursor reconnect |

Expose authenticated read-only routes for the existing root run:

- `GET /v1/runs/{parent}/children`
- `GET /v1/runs/{parent}/children/{child}/history`
- `GET /v1/runs/{parent}/tree-events?after=CURSOR`
- Bounded delegation/preset metadata, without executable configuration or
  credentials.

Validate the parent/child/session ownership on every request. Tree events use
committed global event sequences with run/parent/task identities; reconnect
continues from the cursor without resubmitting a task. The first depth-one
implementation joins only its admitted direct children. Later graph-agent
delegation requires bounded descendant queries and the same top-root budget.

The browser access-session route allowlist currently admits child/history
routes only for `/v1/graph-runs`; extend it for these actual generic routes.
Add corresponding native CLI and thin browser/VS Code client reads. Root
cancellation stays on the existing authenticated run control. Do not add a
public endpoint for spawning arbitrary children, writing outcomes or manually
editing plan state.

The right sidebar keeps Agent as the default. It renders the parent's committed
delegation activity, identified child status, independent histories and actual
metrics, then its observed joined tool result. Reuse the shared child rendering
primitives without requiring a `graph_root` flag or inventing a graph catalogue
entry. Views observe native execution; they do not schedule children, infer
success, sum missing usage or approve mutations implicitly.

## Acceptance and later planning

Acceptance must drive the real parent model/tool loop. With labelled synthetic
provider protocol replies and real native processes/storage, prove:

1. The parent selects two leaf tasks; both child AgentRunners select real file
   reads, preserve separate conversations/receipts/usage, and return findings.
   The parent's subsequent model call uses those actual results and requests
   one real edit through existing controller approval and the effect journal.
2. A child failure causes a later parent-selected investigation with a new
   batch identity. Earlier children/results remain immutable and are not
   replayed. This proves observed follow-up delegation, not mutable DAG parity.
3. Limits, duplicate/mismatched call IDs, unknown presets, injected authority,
   nested delegation and exhausted root budgets reject before unauthorized
   child admission or extra provider calls. Two occupied parent workers do not
   deadlock child scheduling. Two leaf workers racing at the final unheld call
   allowance dispatch at most one additional request, while one real parent
   continuation remains possible. Envelope bounds include UTF-8 JSON escaping
   and metadata, not only the length of result text.
4. Actual admission-event SQL failure rolls back batch/tasks/runs/prompts/budget
   and events together. Actual result/conversation storage failure preserves
   existing child effects and fail-stop ownership without fictional history.
   Concurrent duplicate settlement returns one exact outcome/event; an event
   belonging to another child rejects. A real settlement-event trigger failure
   rolls back event/outcome together without undoing the completed child.
5. Cancellation covers waiting/active children and the parent's subsequent
   effect. A held effect during cancellation retains the existing adapter's
   outcome/uncertainty semantics, without blind retry or lost claims.
6. Crash/reopen preserves isolated history and observed completed effects,
   quarantines unfinished claims, and never relaunches children. HTTP/CLI and
   the shared sidebar adapter reconnect by committed cursor with stable
   identities and no execution replay. Rendered browser/IDE acceptance and
   live-provider acceptance are reported separately when actually exercised.

The next planning phase adds model-selected dependency DAGs and human checks,
with accepted plan revisions and revision-checked mutations. Completed nodes,
admitted children, recorded effects and uncertain claims remain immutable.
Replanning may change only undispatched work; it cannot erase results, turn a
human answer into an effect approval, widen a preset, reset budgets or reuse a
retired child identity. Human pauses must preserve remaining execution
allowances under an explicit backend pause policy. Later remote A2A delegation
records real peer/task identity and scoped credentials rather than inferring
remote completion from a timeout. Skills and context compaction remain separate
native requirements, with instruction authority and same-wire continuation
fidelity preserved.

These stages continue the existing OSS goal: a real shared agent/coding core,
single and graph execution, providers, MCP/A2A, local SQLite through xlang3,
CLI, webpage and VS Code. Private Nexus team-server features remain outside
this OSS design.
