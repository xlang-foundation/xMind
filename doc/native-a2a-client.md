# Native outbound A2A client and remote delegation

## Current boundary

xMind currently exposes an authenticated local A2A server for JSON-RPC A2A
1.0 and the declared 0.3 compatibility wire. The official JavaScript SDK
1.3.0 has exercised that server, including durable tasks, retries, history,
streaming, listing and cancellation. The native `delegate_tasks` tool creates
bounded local read-only agents. xMind does not yet connect to a separately
configured remote A2A agent, and local delegation is not outbound A2A support.

The working tree now contains a low-level native JSON-RPC client for an
owner-configured AgentCard URL. It uses the native HTTP transport, supports
JSON-RPC send/get/cancel operations for A2A 1.0 and the legacy 0.3 method names,
and downgrades only when a version-1 card request returns the A2A
`VersionNotSupportedError` code. It honors a directly configured card path,
checks response correlation, bounds and redacts peer errors, and currently
accepts only interfaces on the configured card origin. A local loopback peer
contract is being added; this does not establish independent SDK acceptance.
The client is not yet registered in the runtime, persistence, AgentRunner,
graph, CLI, browser or VS Code, and it does not yet support streaming or
durable recovery after uncertain remote dispatch.

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
   implementation. Verify the exact current 1.0 and supported 0.3 wire shapes,
   card security, JSON-RPC error mapping, interface binding and state model.
   The [current versioning contract](https://github.com/a2aproject/A2A/blob/main/docs/specification.md#36-versioning)
   requires `A2A-Version` on each 1.0 request; downgrade is permitted only
   after the peer reports `VersionNotSupportedError` (-32009).
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
