# Native outbound A2A client and remote delegation

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
native card discovery, send, get and cancel. The peer harness has passed its
standalone SDK self-check; the native executable's independent interop result
is pending the hosted Windows native gate.
The native client now reads v1 and legacy card security requirements, accepts
only no-auth alternatives or HTTP Bearer, refuses unsupported requirements,
and withholds a configured bearer from subsequent calls when the card does not
advertise bearer authentication. Focused loopback assertions cover refusal and
credential withholding; hosted native compilation and CTest remain pending.
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

An owner-authenticated `POST /v1/operations/{id}/a2a/inspect` now refreshes a
known task using only the peer ID, registration revision, card URL, interface,
and task ID sealed into its succeeded operation. It refuses uncertain sends,
registration/interface changes, and missing task IDs, and persists the peer's
latest returned task snapshot in xlang3 SQLite. The local browser allowlist
forwards this route using the HttpOnly view session. The CLI exposes
`a2a-inspect OPERATION_ID`, and the shared webpage/VS Code operation view offers
an explicit “Refresh remote task” control for acknowledged tasks. Contracts
check owner authentication, same-origin browser forwarding, exact saved task
ID, durable inspection, CLI routing, client identity checks, and that uncertain
dispatches do not issue a second remote effect. This is explicit polling;
streamed observation/resumption, input-required follow-up, cancellation,
artifacts and cross-client rendered acceptance remain open. These are required
before claiming full outbound A2A parity.

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
