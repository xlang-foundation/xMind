# Native dependency plans in ordinary Agent mode

## Registered MCP resource and prompt nodes — 2026-10-14

Dynamic plans now admit exact-bound `mcp_*` aliases for registered MCP
resources and prompts in the coding preset. Each model-free child opens a fresh
peer connection, rediscovers the full catalog, compares the frozen server
revision and binding, and routes the read through the existing controller
approval and durable operation journal. Resource URIs and prompt arguments
come only from that sealed binding and schema. A native subprocess contract
executes an approved resource read followed by a dependent prompt retrieval,
checks both actual responses and confirms that the fixture's effectful tool
was never called. No xlang3 source change was required. See
[validation status](VALIDATION_STATUS.md).

## Automatic A2A stream recovery — 2026-10-14

The shared native A2A tool path now handles a bounded transport interruption
after a streamed send has revealed a remote task ID and the owning run has
committed an explicit peer event cursor. It resubscribes to that same task from
the latest durable cursor, up to two times within the existing run deadline;
it never repeats the initial send. If the task ID/cursor is missing, the run is
cancelled or out of time, or resubscription fails, the effect remains uncertain
for the existing inspection and recovery path.

The actual AgentService contract forces a synthetic A2A 1.0 peer to drop the
initial stream after its first committed task event. It verifies one send,
automatic resubscription from event 1, the final event 2, durable child-owned
event history, and a later SQLite-reopen resubscription from event 2 without
another send. The focused Release CTest passes; the peer and provider are
synthetic, while the native transport, approval, SQLite journal and restart
path are actual. A second case omits the explicit cursor before disconnecting
and verifies the operation remains uncertain and no resubscription occurs.
Live external-peer behavior remains unverified.

## Dynamic deterministic workspace tools — 2026-10-14

Dynamic plans admit native `tool` nodes for the fixed read-only workspace
aliases `read_repository_instructions`, `read_file`, `list_files`,
`search_files`, `find_files` and `glob_files`. Arguments are checked against the
sealed native tool schema and the frozen `workspace.inspect` capability before admission.
Execution uses the admitted workspace in a separate owned child, makes no model
request, and records ordered `tool.started`/`tool.completed` receipts. SQLite
reopen/recovery verifies those receipts against the immutable node definition.
The repository schema migration is now v23.

The same node type accepts an exact MCP tool alias only when its complete tool
binding and input schema are sealed in the finalized root catalogue and the
tool belongs to `workspace.coding`. Execution launches a fresh client from the
frozen server configuration, rediscovers and compares the peer binding, then
invokes the shared MCP registry. The registry owns approval, operation
journaling, secret-reflection checks and uncertain-outcome quarantine.

The node type also accepts `run_process` through the registered coding
preset's sealed profile schema. It uses the existing native `ProcessExecutor`,
requires the usual exact controller approval, captures guidance for its
selected working directory, retains output in the process artifact journal and
must finish within the owned child. Process nodes are restricted to the
admitted primary workspace folder; background jobs are refused because their
lifetime cannot be joined to a terminal plan child. A native process contract
approved a real Node process in a nested workspace directory, verified its
actual file write through a dependent native read, and verified denial before
dispatch. Recursive plans and replay of uncertain effects remain unavailable
as plan nodes.

The dynamic-plan pure, repository, engine and MCP/process contracts pass 4/4
locally after the process-node change. The actual direct process/MCP contract
uses synthetic provider replies and credentials; process launch, MCP peer,
approval decisions, output artifacts, file effects, and embedded xlang3 SQLite
are native and actual. The full native Release suite completed at 143/144
before process nodes were added; the single Rust Analyzer call-site-definition
failure passed its immediate isolated rerun. VS Code and browser suites passed
343/343 and 109/109 at the preceding MCP checkpoint. External-browser and
installed VS Code acceptance remain open. Recursive planning and full
coding/provider parity remain work.

The dynamic coding-child MCP checkpoint passed the complete guarded local
**78-contract native gate in 145.17 seconds**, with the exact complete manifest
and all **427 frozen native/build-source input hashes** verified. The new
contract passed in **3.08 seconds** and covered approved peer writes followed by
an independent native read, denied approval, child-only negotiated-protocol
drift, cancellation before dispatch, and a write with a lost reply. Its five
scenarios asserted **16 synthetic provider requests, 13 actual MCP peer discovery
cycles and two actual MCP dispatches** through AgentService, owned coding/inspect
children, exact approvals and embedded-xlang3 SQLite. The peer is an independent
legacy-wire fixture; this is neither an upstream SDK nor live-provider result.
[Local78 source, logs and limits](evidence/native-dynamic-mcp-local-provenance.json).

The earlier hosted 3fc gate below remains a separate 77/119/26 result. Frontend
sources are unchanged and those suites were not rerun for this native contract.
The installed preview remains ace/schema v10; schema-v11 installation, rendered
planning and installed VS Code acceptance remain pending. No SDK fix was made
for this checkpoint.

Exact source `3fc420480f61db75d3efaf4bda77fbefd8246166` subsequently passed its [hosted gate](https://github.com/xlang-foundation/xMind/actions/runs/37808089507): **77 native contracts in 206.89 seconds**, **119 extension tests in 2.4612639 seconds** and **26 browser tests in 1.3989153 seconds**. All 16 job steps passed, with the exact complete native manifest, zero failures/skips, model-free browser/native integration and 18 verified VSIX assets. Original logs, advertised archive digests and exact source/runtime maps were verified. Provider replies remain synthetic; this establishes the source checkpoint rather than installed or live planning acceptance. The installed preview remains ace/schema v10. [Artifact-bound hosted evidence](evidence/native-dynamic-plan-hosted-provenance.json).

The initial schema-v11 source increment passed the complete guarded local gate:
**77 native contracts in 153.88 seconds**, **119 extension tests** and **26 browser
tests**, with zero failures/skips and no exclusions. The exact registered and
expected native sets match; 421 native/build-source and 32 view-source hashes
were verified. Rebuilt server/browser adapter integration and verification of
18 required packaged VSIX assets also passed at their separate scopes.
[Source, logs and limits](evidence/native-dynamic-plan-local-provenance.json).
The installed preview remains the separately verified `ace2460` schema-v10
backend. Live-provider planning and installed VS Code acceptance remain pending.

The disposable preview migration acceptance has not passed. A separate file-only
comparison using the exact hosted xlang3 runtime reproduced a Windows file-open
gap: identical bytes read successfully at a 226-character path and fail with
`FileNotFoundError` at 288 characters. The closed-ledger reproduction fails while
opening the generated schema file, before connecting SQLite. Both failed fixture
attempts and the new raw diagnostics are retained privately; no installed preview
or native SDK source changed. The [derived probe summary](evidence/native-dynamic-plan-preview-probe.json)
records exact hashes, scope and remaining upgrade checks.

An eligible ordinary Agent receives `plan_tasks`, `revise_plan` and
`inspect_plan`. The model can select agent tasks and human questions, then
revise work that has never been claimed. A plan belongs to the same ordinary
Agent root throughout execution. It is not a registered graph, and its model
arguments cannot supply owners, runtime settings, provider endpoints or grants.

Native C++ owns scheduling, authority, budgets, effects and run lifecycle.
Repository transactions use embedded xlang3 SQLite. CLI, browser and VS Code
observe the same committed records and submit authenticated controller input.
The broader [planning design](native-dynamic-plan-design.md) remains the target
for additional deterministic tools, other node kinds and recursive execution.
The separately documented [nested planning contract](native-dynamic-plan-recursion.md)
specifies the current root-only ownership boundary and the required migration,
shared-budget, recovery and client acceptance work.

## Tasks, dependencies and revisions

| Preset or node | Implemented boundary |
| --- | --- |
| `workspace.inspect` revision 1 | Native read-only child AgentRunner, at most four model turns |
| `workspace.coding` revision 1 | Child AgentRunner inheriting the root's enabled edit/create/process/MCP authority and existing exact approvals; bounded by the admitted policy |
| `tool` | Native model-free workspace reads, plus exact-bound MCP tools dispatched through the normal controller approval and operation journal |
| `human` | Backend-published question answered through the authenticated controller, with a fixed native expiration policy |

The coding preset is offered only when the root has an enabled coding/effect
capability. It cannot widen that capability. An inspect child cannot edit or
execute processes; a coding child can request an effect only through the
existing permission and operation journal. A human answer is dependency data
and never supplies effect approval.

Dependencies use `success` to require a successful observed outcome, or
`observed` to allow a dependent task to examine a recorded failure. Cycles and
unknown labels reject before admission. Claimed, published, settled, skipped,
cancelled and effect-linked definitions remain protected. A revision may add,
replace or skip only undispatched mutable work. Retired labels cannot be reused.
The backend records accepted topology revisions and the actual provider call
that selected each change.

An accepted planning call must be the sole tool call in its assistant turn.
The native engine retains that exact finished response and raw arguments,
executes the owned frontier, then commits the actual result and signed provider
receipt once before reserving its held continuation. A rejected proposal uses
a bounded typed error and consumes its actual model turn. It cannot refund
calls or hide an accepted execution failure inside a generic tool response.

## Shared limits and clean pauses

Legacy delegation and dependency plans share one child pool per AgentService:
four workers, 64 pending jobs, at most two concurrent leaves per root and eight
executable children over that root's lifetime. Every model attempt counts
against the same 32-call root budget; held parent continuations count too.
Planning additionally admits at most 16 parent turns, 32 lifetime labels, 16
revisions and eight published or reserved human questions. Actual captured
policy and counters are returned by the backend rather than inferred by views.

An ordinary root is eligible only when its configured provider supports tools,
its workspace exists and its configured turn limit is at most 16. The engine
does not silently reduce a higher configured limit to enable planning.
Registered graph agent nodes retain their existing graph owner and do not
start an independent planning root.

Waiting for a human closes the measured active execution segment only after
owned child and peer work has retired. Closed human wait does not consume
active execution time. Clean paused roots remain admitted and prevent a
provider generation change. Restart adopts a clean paused owner without
automatically replaying a call or scheduling a continuation. The controller
can explicitly resume an already answered, ready pause under its current
revision and state sequence. Question expiry fails the paused root without a
new provider call.

Unrecorded outcomes or commit faults stop the execution generation and retain
claims and quarantine. Recovery does not replay unknown effects. Stale paused
capabilities remain inspectable and cancellable, while input/resume rejects
before changing their records. Completion with unfinished planned work fails
explicitly.

## Controller protocol

| Route | Purpose |
| --- | --- |
| `GET /v1/agent/planning` | Current executor capability and available planning tool names |
| `GET /v1/runs/{root}/plan` | Owned topology, readiness, questions, accepted revisions/calls, captured policy and actual budget counters |
| `POST /v1/runs/{root}/plan/human/{request}` | Exact JSON object text plus expected revision/state sequence for an observed question |
| `POST /v1/runs/{root}/plan/resume` | Explicit resume using the expected revision/state sequence |

These commands use the server's existing access authentication. The native
controller supplies the answering actor; model and view data cannot select it.
Human input retains its exact JSON text and is bounded to 16 KiB before
publication. Public call observations contain correlation IDs and, while a
response is held, only supplied model/usage/timing fields. Private capability
hashes, tool catalogues, credentials and opaque provider receipts are excluded.
Owned child histories retain actual response metrics separately.

## Verification scopes

The registered contracts cover distinct boundaries:

- Pure plan validation/reduction and native authority identity have no provider
  or SQLite execution claim.
- Repository and HTTP projection fixtures use labelled synthetic controller
  inputs/provider DTOs with actual native transactions and transport.
- The engine peer supplies synthetic Anthropic responses and signatures while
  real AgentRunners perform parallel reads, a failed investigation, a protected
  revision, clean human pause/reopen, separately approved coding and a dependent
  verification read. It also covers final-human-only continuation, cancellation
  and expiry.
- Shared renderer/controller fixtures validate backend identity, stale input,
  escaping and supplied metrics; they do not establish an installed IDE result.

None of these fixtures establishes live-provider planning, installed VS Code
acceptance, rendered process-plan acceptance, recursive planning,
full coding/provider parity or private Nexus execution. The separate
browser/native integration uses disposable model-free state to check graph,
human, authentication, durable cookie and reconnect boundaries; it does not
establish end-to-end dynamic planning in a live webpage or installed VS Code.
The actual modern/legacy MCP registry checks and SQLite binding negatives do
not establish a real dynamic coding-child MCP peer effect on their own. The later local 78-contract
contract does: its coding child rediscovers the exact nine-field private MCP
binding, proposes a child-owned journal operation and waits for actual controller
approval before the peer appends bytes. A dependent inspect child reads those
bytes independently; the peer acknowledgement still reports
`independently_verified: false`. Protocol-only child drift rejects before its
model request or proposal, denial/cancellation produce no peer dispatch, and a
lost reply retains an uncertain operation/node without verifier dispatch or
replay after reopen. This contract does not separately prove a newly approved
root is blocked by the retained quarantine; existing direct-MCP contracts cover
that boundary. It also does not establish live or installed IDE MCP effects.

## Retained failed gates and corrections

The first complete gate for the additional MCP contract passed **77 of 78** in
**159.80 seconds**. Its synthetic initial coding proposal omitted the required
empty `depends_on` array, so native admission rejected it before child approval,
peer dispatch or a file effect. Adding only `depends_on: []` to that proposal
preserved production validation, all five scenario assertions and their exact
request-count checks. The second complete gate passed all 78; no production C++
change was needed. The first failed source, log and disposable fixture remain
retained and are not counted as a pass.
[Failed attempt and corrected full gate](evidence/native-dynamic-mcp-local-provenance.json).

The first complete native gate passed 68 of 77 contracts in 239.52 seconds.
A shared condition variable allowed the expiry observer to consume a worker
wakeup. The fix gives expiry its own condition variable; a regression contract
requires four successive real provider streams across expiry ticks. The HTTP
fixture also needed the same canonical provider identity used at admission.
The process drift fixture now requires the earlier sealed-configuration
rejection, preserving its no-effect and exact provider-request checks.
[Original first failure](evidence/native-dynamic-plan-first-failed-ctest.log).

The second complete gate passed 76 of 77 in 154.28 seconds. Its remaining engine
failure exposed an operation-boundary mismatch: edit execution journals
`replace_file`, while the model catalogue offers `edit_file`. The backend now
maps only its known executor names before checking the captured preset. MCP
effects similarly require their exact privately sealed registry binding and
server resource; an alias or schema alone cannot authorize a different server
configuration. Claim ownership, workspace, readonly policy and exact approvals
remain enforced. Regression negatives require unchanged journals and audit
records on rejection. [Original second failure](evidence/native-dynamic-plan-second-failed-ctest.log).

The corrected, frozen source then passed all 77 contracts. The engine contract
completed in 1.56 seconds and performed the real approved coding edit plus
dependent verification read. [Final complete CTest output](evidence/native-dynamic-plan-final-ctest.log).
The failed gates remain separate and are not counted as passes.
