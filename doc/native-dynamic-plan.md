# Native dependency plans in ordinary Agent mode

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
for later node kinds and recursive execution.

## Tasks, dependencies and revisions

| Preset or node | Implemented boundary |
| --- | --- |
| `workspace.inspect` revision 1 | Native read-only child AgentRunner, at most four model turns |
| `workspace.coding` revision 1 | Child AgentRunner inheriting the root's enabled edit/create/process/MCP authority and existing exact approvals; bounded by the admitted policy |
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
acceptance, dynamic deterministic tool/MCP/process nodes, recursive planning,
full coding/provider parity or private Nexus execution. The separate
browser/native integration uses disposable model-free state to check graph,
human, authentication, durable cookie and reconnect boundaries; it does not
establish end-to-end dynamic planning through a rendered view. The actual
modern/legacy MCP registry checks and SQLite binding negatives do not establish
a real dynamic coding-child MCP peer effect.

## Retained failed gates and corrections

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
