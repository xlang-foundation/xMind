# Native outbound A2A client and remote delegation

## Repository instructions and approved sends — current source

Ordinary AgentRunner A2A calls bind the delivered root repository guidance
before publishing an operation. If that guidance changed since the model
request, the call returns repository_instructions_required without a proposal
or remote send. The registered tool also verifies the same native precondition
after approval; a change retires the operation as failed with the recorded
repository_instructions_changed_before_a2a_dispatch reason. Controller approval
does not make stale instructions current. A later call must consider the new
context and obtain its own exact approval.

A native regression exercises both change windows and unchanged approved
delegation through the real agent/model/tool loop, operation journal, HTTP
transport and peer-created file. Model/A2A responses are labelled synthetic.
Both change windows sent a remote task before this correction; afterward
they send none, while the unchanged case sends exactly once. The next model
request receives the changed instructions. This is a source-level instruction
boundary fix; installed, hosted, rendered and broader A2A acceptance retain
their separately recorded scope. [Red/green evidence](evidence/a2a-agent-guidance-20261018.json).

## Current boundary

xMind currently exposes an authenticated local A2A server for JSON-RPC A2A
1.0 and the declared 0.3 compatibility wire. The official JavaScript SDK
1.3.0 has exercised that server, including durable tasks, retries, history,
streaming, listing and cancellation. The native `delegate_tasks` tool creates
bounded local read-only agents. AgentRunner now discovers enabled,
owner-registered peers and exposes them to ordinary agent and graph nodes as
typed outbound A2A tools. Read-only child agents do not inherit remote access.

Interop baseline: official A2A protocol tag `v1.0.1`
(`3303592588e388e62e0f69f701af531d2f4e3991`), with legacy wire coverage from
tag `v0.3.0`. The official JavaScript peer is pinned as `@a2a-js/sdk` 1.3.0
with its package integrity in `Native/tests/sdk/package-lock.json`.

The working tree now contains a low-level native JSON-RPC client for an
owner-configured AgentCard URL. It uses the native HTTP transport, supports
JSON-RPC send/get/cancel operations for A2A 1.0 and the legacy 0.3 method names,
and downgrades only on an explicit version-not-supported response that
advertises 0.3 support. It honors a directly configured card path, checks
response correlation, bounds and redacts peer errors, and currently accepts
only interfaces on the configured card origin. Its local loopback peer contract
is included in the native CTest suite. A second CTest contract launches the
pinned official JavaScript SDK 1.3.0 as an authenticated peer and exercises
native card discovery, send, get, streaming and cancel. The fresh current-source
Windows Release build passed this native executable contract on 2026-10-15;
the exact binary and CTest evidence is in
[`native-a2a-sdk-bidirectional-20261015.json`](evidence/native-a2a-sdk-bidirectional-20261015.json).
The native client now reads v1 and legacy card security requirements, accepts
only no-auth alternatives or HTTP Bearer, refuses unsupported requirements,
and withholds a configured bearer from subsequent calls when the card does not
advertise bearer authentication. Focused loopback assertions cover refusal and
credential withholding; hosted native compilation and CTest remain pending.
The current checkout rebuilt the Release server and outbound/registry
contracts and passed the official-SDK outbound, inbound HTTP and registered
agent tests (**3/3**, 3.18 seconds). This validates those pinned SDK paths on
the current native source; peers and provider responses remain fixtures.
[Current-source build and test evidence](evidence/native-a2a-current-checkout-20261015.json).
An owner-managed native registry now persists peer IDs, card URLs, enabled
state and revisions through xlang3 SQLite. Bearer bytes are stored in the
scoped encrypted credential repository; catalog and API metadata contain only
an opaque reference and a `has_bearer` flag. Rotation publishes the new
reference before deleting the old credential. The owner API now exposes
revision-checked list/register/enable/remove operations, accepts credentials
only in the registration request, and never returns them. The local browser
adapter allowlists these routes for the same-origin local view; it forwards
the view-session credential internally and never asks the user for a server
token. Native and adapter contracts cover owner authentication, revision
conflicts, no-secret responses, route boundaries, rotation, clearing, retired
IDs and restart behavior. The native CLI exposes `a2a-agents`, `a2a-register`,
`a2a-enable` and `a2a-remove`; registration reads an optional bearer from a
named environment variable and redacts it from output. Its Windows HTTP
contract is pending. AgentRunner uses a per-run discovery snapshot, an
operation-bound message ID, controller approval of the exact prompt and peer
revision, and a final registration check before dispatch. It records returned
task/context IDs. A failure after the send boundary or an unusable acknowledged
reply becomes a durable uncertain operation, and the run stops without retrying
the remote effect. The extended native contract covers approved dispatch,
denial without dispatch, exact message identity, returned task IDs and
post-dispatch failure quarantine; hosted Windows build/execution is pending.

If a streaming event has already exposed a task ID before a later stream or
event-journal failure, the uncertain operation now retains that task/context
identity. The owner can issue read-only `a2a/inspect` against the exact saved
peer registration and task ID, including after xlang3 SQLite close/reopen. The
operation remains uncertain; observation does not acknowledge the original
send, resume the stream, or enable cancellation/follow-up. An uncertain send
without an observed task ID still cannot be inspected or replayed. The focused
native contract injects failure after the first task event and verifies no
second send. Current native, VS Code and browser test results are recorded in
[observed-task recovery evidence](evidence/native-a2a-observed-task-recovery-local-20261011.md).

An owner-authenticated `POST /v1/operations/{id}/a2a/inspect` now refreshes a
known task using only the peer ID, registration revision, card URL, interface,
and task ID sealed into its succeeded operation. It refuses uncertain sends,
registration/interface changes, and missing task IDs, and persists the peer's
latest returned task snapshot in xlang3 SQLite. The local browser allowlist
forwards this route using the HttpOnly view session. The CLI exposes
`a2a-inspect OPERATION_ID`, and the shared webpage/VS Code operation view offers
an explicit “Refresh remote task” control for acknowledged tasks. An explicit
`--include-artifacts` CLI option and “Fetch remote artifacts” control request
A2A `GetTask` with `includeArtifacts: true`; this keeps artifact transfer
opt-in and leaves ordinary refreshes small. Returned artifact data stays in the
read-only task snapshot and is displayed as untrusted JSON. xMind does not
follow artifact URLs or write remote content into the workspace. The native
inspection limit remains 384 KiB for the task snapshot. Contracts
check owner authentication, same-origin browser forwarding, exact saved task
ID, durable inspection, CLI routing, client identity checks, and that uncertain
dispatches do not issue a second remote effect. This is explicit polling;
initial task events now stream for peers that advertise the capability, while
manual resubscription is available through a bounded owner route described
below; automatic recovery and cross-client rendered acceptance remain open.
These are required before claiming full outbound A2A parity.

The shared Browser/VS Code operation panel, CLI command a2a-resubscribe
OPERATION_ID, and authenticated local HTTP API expose an explicit
POST /v1/operations/{id}/a2a/resubscribe operation.
It accepts only a succeeded delegation or an uncertain send whose recorded
stream already exposed a task ID. The backend resolves the same enabled peer,
checks its saved registration revision, card URL, protocol version and
interface, then calls A2A `SubscribeToTask`/`tasks/resubscribe` with that saved
task ID. It never repeats `SendMessage` or `SendStreamingMessage`. Each
identity-matching event is capped at 64 events and 512 KiB total, appended to
the original run's durable `a2a.task_event` journal, and returned to the
client. The request waits up to 20 seconds for the peer stream to close; events
received before a timeout remain in run history even when the request returns
an error. A peer that does not advertise streaming or refuses a subscription
(for example, because its task is terminal) returns an explicit error, and
read-only task inspection remains available.

When an SSE frame includes a valid `id`, xMind stores that peer cursor in the
same durable event record and sends the latest stored value as `Last-Event-ID`
on the next manual resubscription. This survives a backend restart because the
cursor is recovered from the original run journal. A2A does not require peers
to emit SSE IDs or define whether resubscription backfills missed events, so
this remains best-effort: a peer that omits or ignores the cursor may replay
events. xMind does not claim exactly-once stream delivery. Each local batch is
marked with a distinct subscription ID and per-batch sequence rather than
pretending those frames are new remote effects. Native, browser-adapter/controller
and VS Code host/renderer contracts cover the saved identity, authenticated local view,
bounded response, durable run journal and no-send-replay behavior. These
contracts do not establish rendered/installed-client acceptance or automatic
stream resumption after backend restart.

An explicit outbound cancellation route is now also wired to acknowledged
operation IDs. The owner or enrolled same-origin browser view must send a
separate cancellation command; local run cancellation and disconnect never
cancel a remote task. Before sending the mutating cancellation request, the
backend durably creates a one-shot control record bound to the saved operation,
peer revision and task ID. The record is written with create-only compare-and-swap so concurrent or
later requests cannot dispatch another `CancelTask`. The returned task state
is the peer's actual acknowledgement. Any lost reply, timeout, malformed
response, or persistence failure after the one-shot record leaves the outcome
unknown and requires inspection; it is never replayed. The webpage asks for
browser confirmation and VS Code asks through its modal confirmation before
sending. This path currently exercises v1 JSON-RPC `CancelTask`; independent
SDK cancellation, post-restart rendering of the durable control outcome,
automatic stream resumption, artifacts and complete peer acceptance remain
open.

An input-required remote task can now receive a sequenced follow-up through
`POST /v1/operations/{id}/a2a/follow-up`, the `a2a-follow-up` CLI command, and
the shared webpage/VS Code operation panel. The server rechecks the registered
peer revision and interface, fetches the saved remote task, and accepts a
follow-up only while that task reports `input-required`. Each user message is
bound to its operation, agent revision, task ID and monotonically increasing
sequence in xlang3 SQLite. A compare-and-swap claim precedes the send boundary;
the peer acknowledgement is persisted, while a lost reply is marked uncertain
and is never replayed. A follow-up known not to have dispatched can be retried
only with the same text. Inspection returns the durable sequence and retry
state for reconnecting clients. The native contract covers owner/view
authentication, task/context and message binding, ordered acknowledgments,
duplicate rejection, uncertainty quarantine and restart inspection. Hosted
Windows compilation and execution of this expanded contract remain pending.

The native outbound JSON-RPC client exposes capability-gated SSE methods for
`SendStreamingMessage` and `SubscribeToTask`, mapping their A2A 1.0 and 0.3
method names. It validates the response media type/version, JSON-RPC request
correlation and event boundaries before delivering one result object per SSE
event, with bounded frame sizes and the existing transport deadline and
cancellation behavior. `A2aAgentToolRegistry` now selects the streaming send
when the AgentCard advertises it, retaining blocking `SendMessage` for peers
without streaming. The AgentRunner persists each remote result as an
`a2a.task_event` while the call is active, so the existing event stream,
reconnect and run-history paths can observe it; the operation still records the
final acknowledged task and follows the same no-retry uncertain-outcome rule.
The registry contract checks two delivered events using the A2A 1.0
`StreamResponse` wrappers (`task` and `statusUpdate`) and durable event counts.
The pinned official JavaScript SDK peer now advertises and serves streaming;
its own self-check exercises `sendMessageStream`, and the native CTest contract
consumes that peer's wrapped events. That outbound contract now passes on the
fresh current-source Windows Release build. The shared browser/VS Code activity
panel labels each event with peer, task state, task ID and event sequence, then
retains the full JSON payload for inspection. Rendered live-stream browser and
installed VS Code acceptance are still required.

This is an implementation requirement, not a claim of A2A completion. See the
[local server boundary](A2A.md), [v1 wire](native-a2a-v1.md),
[streaming](native-a2a-streaming.md) and the [AgentFlow plan](../AGENTFLOW_PLAN.md).

## Product contract

Remote agents must be registered by the local owner with a stable ID, endpoint,
protocol preference, enabled state and credential reference. The model may
choose a registered agent ID but cannot supply a URL, headers, credential,
protocol version or tenant. Agent cards, descriptions, skills, messages,
artifacts and all remote results are untrusted data. A card can describe
capabilities; it cannot grant xMind local tools or workspace access.

Implement the outbound client in native C++ and expose it through the shared
agent and graph runtime. The owner API, CLI, browser and VS Code should inspect
the same registration, task, stream, approval and recovery state. Credential
bytes stay in the xlang3-backed encrypted credential store and are never
returned through configuration or task APIs. Each credential is bound to the
exact remote registration and purpose. Do not reuse MCP tokens or assume that
an A2A peer's bearer security scheme means the same thing as local xMind auth.

Discovery must validate the fetched card against the configured endpoint and
select only a pinned, implemented interface/version/binding. Send messages
with stable xMind-owned message IDs and retain the peer's task/context IDs once
observed. Support message send, streamed send, task inspection, subscription or
resubscription, cancellation, task listing where advertised, content/artifact
projection, and the versioned task states. Represent `input-required` as a
durable pause that accepts a typed follow-up message. Unsupported media,
security schemes, interface bindings and operations fail explicitly; never
silently flatten them into successful text.

## Durable effects and authority

An outbound message can start work on another machine. Persist an operation
record bound to the parent run/session, remote registration revision, exact
message, message ID, deadline and dispatch phase before sending bytes. After
dispatch, a timeout, cancellation or lost response is an uncertain remote
outcome; never replay the effect just because the response was lost. If a task
ID is known, recover by inspecting that task. If dispatch may have happened but
no task ID was received, preserve an explicit unresolved operation for owner
inspection. Reopening xMind must not repeat the message or infer remote
cancellation.

Remote task state is observed input. Only the native controller can commit
local operation/graph transitions. A remote failure or cancellation maps to a
recorded local outcome without erasing its observed task history. Stream
disconnect stops observation but does not cancel the remote task. Explicit
cancel is a separate, authenticated peer request whose result must be observed.
Human approvals and workspace permissions remain local and scoped to the
registered delegation policy.

## Acceptance gates

1. Pin the A2A specification revision and SDK peer revision used for this
   implementation. Verify the exact current 1.0.1 and supported 0.3 wire
   shapes, card security, JSON-RPC error mapping, interface binding and state
   model. The [pinned 1.0.1 specification](https://github.com/a2aproject/A2A/blob/v1.0.1/docs/specification.md#64-version-negotiation-error)
   requires `A2A-Version` on versioned requests and specifies an
   `application/problem+json` version error with `supportedVersions`; the
   client may fall back only when that list includes 0.3.
2. Use the official SDK as an independent native-client peer. Verify discovery,
   successful send, task return, `GetTask`, stream and resubscription,
   `input-required` follow-up, cancellation, artifacts, malformed cards,
   unsupported versions, denied credentials, deadline and server loss. Record
   exact requests and responses; a local echo fixture is protocol evidence only,
   not real model acceptance.
3. Integrate a registered remote agent as a typed, permission-checked tool and
   graph node. A real local AgentRunner and graph must dispatch to the peer,
   stream observations into the shared event/history contract, pause/resume,
   cancel, and report actual remote artifacts without granting them local
   authority.
4. Inject failures before dispatch, during partial send, after peer admission,
   after a task ID, during streaming, and while recording results. Verify
   exactly when retry is safe, when the operation is uncertain, and how it is
   inspected/recovered after xlang3 SQLite reopen. Verify no unintended second
   remote effect and no fabricated terminal state.
5. Exercise the same task through CLI and VS Code, including owner approval,
   stream detach/reconnect, task inspection and cancellation. Verify credential
   redaction in logs, history, run events, errors, browser responses and VS Code
   messages.
6. Test every transport and security mode advertised by the pinned contract.
   Any unsupported transport or content type remains an explicit parity gap;
   official SDK success on JSON-RPC alone does not establish full A2A support.

Until these gates pass, outbound remote delegation, remote task recovery,
cross-client inspection and full A2A interoperability remain open in
[`PARITY.md`](PARITY.md).
