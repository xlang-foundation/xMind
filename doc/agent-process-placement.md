# Agent execution placement

## Current behavior

The native `AgentRunner` is the shared C++ model/tool loop. `AgentService`
executes root runs in its bounded in-process worker threads. Graph nodes and
delegated children use the same loop through `NativeChildExecutor`; they do not
currently select a separate process. The existing `xmind worker` mode hosts a
trusted xlang3 callable over the xlang3 LRPC interface. It isolates that
callable, not the C++ agent/model/tool loop.

This leaves the user's requested choice between in-process and out-of-process
agent execution unfinished. Process placement is an execution policy, not a
different kind of agent, tool, graph, or model. Both placements must execute
the same `AgentRunner` and preserve the same backend-owned workspace, provider,
permission, MCP/A2A, graph, budget and persistence rules.

## Target contract

Trusted agent and graph configuration may select `in_process` or `process`.
`in_process` remains the default. The model cannot select placement or launch
commands. A process-bound run keeps its existing durable run and session IDs;
all clients continue to inspect and control it through the same xMind Server.

The native supervisor owns worker admission, executable identity, process
creation time, cancellation and retirement. The worker must report actual
startup and terminal outcomes, and the supervisor must reconcile those with
the durable run and operation journals. A lost reply or worker crash must not
cause the model/tool loop or an external effect to replay. An effect whose
outcome cannot be proven remains uncertain and requires explicit recovery.

Worker communication should use a private, versioned xlang3 IPC contract after
the selected LRPC/shared-memory ABI has been verified on each supported host.
The endpoint must be bound to the launched worker's process identity and a
fresh per-run secret; a port or shared-memory name alone is not authentication.
Provider credentials, workspace grants and command authority must never appear
in command-line arguments, ambient environment variables, or client-visible
messages. The worker must receive only a sealed server-created execution
descriptor and resolve secrets through the backend-owned profile.

The existing product uses one `xmind` executable. A worker is an internal mode
of that binary, not a user-authored Python entry point. The xlang3 tool worker
remains a separate feature and cannot stand in for process-bound AgentRunner
execution.

## Implementation and acceptance sequence

1. Finish an `AgentExecutionHost` capability boundary for model calls, run and
   context journals, tool discovery/invocation, approval waits, and effect
   receipts. Commands and results carry owned bounded values; credentials,
   SQLite handles and backend runtime objects stay in the owner. The
   `AgentRunJournal`, `AgentContextJournal` and `AgentModelGateway` seams now
   exist. Workspace model-tool calls also cross a typed, run-scoped invocation
   envelope; MCP/A2A and effect dispatch still use concrete backend services.
2. Keep the current in-process implementation as the first host adapter. Run
   the same AgentRunner contract through this host and the existing path,
   covering provider continuations, workspace reads, approved edits, process
   tools, MCP/A2A calls, metrics and durable events before adding another
   placement.
3. Pin the tested xlang3 native-host branch revision in the xMind integration,
   then add the native worker supervisor and private IPC protocol. Keep run
   admission, grants, credentials, journal ownership and recovery in the
   backend. Bind every request and reply to the run, immutable configuration
   revision, worker process identity and protocol version.
4. Add backend-owned placement configuration for primary agents, subagents and
   graph agent nodes only when both hosts execute the same capability contract.
   Reject unknown values and keep `in_process` as the default. Then route opted
   in runs through the worker without changing the HTTP/SSE or CLI interfaces.
5. Exercise cancellation during provider streaming, tool execution and human
   pause; worker startup failure; crashes before and after durable effects; lost
   replies; server shutdown; and restart/reconciliation. Prove completed tools
   are never replayed and unresolved effects remain quarantined.
6. Accept through the installed webpage and VS Code clients, and verify that
   the same process-bound run remains inspectable and controllable after either
   view reconnects.

### Pinned xlang3 LRPC transport audit — 2026-10-17

The pinned xlang3 revision exposes native C++ wrappers for `lrpc_listen`,
`register_remote_object`, `import_remote`, and `probe_remote`; its Windows and
POSIX transports use shared memory. On this Windows host, the pinned checkout's
Release LRPC smoke, server-exit, backpressure, and backpressure-after-server-
exit tests all pass. This verifies the transport for the selected xlang3 pin,
including endpoint retirement and bounded-slot pressure behavior.

LRPC transports calls to xlang objects. It does not by itself implement an
owned-value AgentRunner protocol, durable run callbacks, provider streaming,
workspace/effect capabilities, or process-placement policy. The C++ AgentRunner
must stay behind a separately versioned/authenticated contract; LRPC remains a
candidate transport only. The existing xlang3 callable worker is not evidence
that the coding agent loop can run in another process.

### Current-source authenticated xlang3 tool worker — 2026-10-17

Built `xmind.exe` from xMind `e8782c4` and started its private `worker` mode as
a child process with a test-only 32-byte capability delivered through stdin.
A separately compiled C++ xlang3 client verified that an unauthenticated call
was denied, the HMAC-authenticated handshake succeeded, the trusted fixture
function returned `{"result":"worker:xMind","count":5}`, and replaying its
sequence was denied. The worker was then terminated by the test harness. The
binary and probe digests, runtime pin, fixture, and exact output are recorded in
[`agent-worker-lrpc-current-source-20261017.json`](evidence/agent-worker-lrpc-current-source-20261017.json).

This verifies the current-source xlang3 callable worker and its request
authentication. It remains a tool worker; it does not execute `AgentRunner`
out of process or establish the process-placement target above.

### Process-tool host seam — 2026-10-18

`AgentRunner` and `GraphRunner` now consume process-tool discovery, invocation,
wait, stop and run-join through `AgentProcessToolHost`; the existing
`ProcessExecutor` is the in-process implementation. The Release
`agentflow_process_effects`, `agentflow_agent`, and graph execution libraries
compile. The updated AgentRunner contract passes against the synthetic
inference peer and real filesystem/process tools, including process execution
in an admitted attached workspace folder. The contract used the current-source
AgentRunner test executable and the matching isolated Release xMind worker
binary.

This seam still borrows `WorkspaceToolSet`, stop tokens and native instruction
verification callbacks. It does not yet define owned-value IPC messages, allow
remote-host injection, or execute `AgentRunner` out of process. MCP/A2A,
approval and other workspace-effect dispatch also remain in the concrete
runner. It is a first capability boundary, not process-placement acceptance.

### Run-scoped workspace invocation boundary — 2026-10-18

The model loop now submits read-only workspace tool calls using an owned request
with run, attempt, call, workspace revision, folder, tool and bounded argument
metadata. The request contains no path, OS handle, credential or database
object. Both the single-folder host and immutable multi-folder host verify the
workspace revision, selected folder and exposed tool before dispatch. The
request has a strict versioned JSON encoding with duplicate, unknown, missing,
oversize and out-of-range fields rejected. That encoding does not provide peer
authentication, replay prevention or active-run authorization; a future IPC
adapter must add those checks. The existing `AgentRunner` path remains the
owner and invokes the same local hosts; this is not yet the complete
`AgentToolHost` or an IPC contract.

The focused Debug and Release current-source workspace-tool and AgentRunner
contracts passed 2/2 in each configuration against xMind `91d41f0`. Coverage
rejects stale membership revisions, mismatched folders, hidden tools,
duplicate/unknown wire fields, unsupported protocol versions, oversized or
out-of-range metadata, and malformed run metadata, while proving valid reads
still reach the selected admitted folder. This validates the backend change;
the installed VS Code runtime was not rebuilt or activated for this checkpoint.

No process-placement config or C++ AgentRunner worker is implemented, and the
full process-placement contract remains unaccepted. The run/context journals
and model gateway are tested persistence/provider seams; they do not by
themselves make AgentRunner portable. This document records the requested
direction and the testable boundary; it is not a completion claim.

### xlang3 native-host prerequisite — 2026-10-17

The native C++ host API was introduced on xlang3 branch `xmind-cpp-lrpc-host`
at `ad7e74058ebdc941ec656c315a8a47a01c12d74e`. xMind now pins descendant
`b0da71f22e9591b6bd99883b21c50ad85dbe4bed` from branch
[`xmind-fastapi-pydantic-compat`](https://github.com/CantorAI/xlang3/tree/xmind-fastapi-pydantic-compat);
it retains the LRPC host API and adds pydantic-core package metadata required
by xlang3 pip. The native host/second-runtime contract passed locally at the
original host revision. The hosted Release gate for the current pin is queued
at [run 39083212989](https://github.com/xlang-foundation/xMind/actions/runs/39083212989).
This establishes neither process-bound AgentRunner execution nor its
per-run authentication, durable recovery or client acceptance. [Original host
build and test evidence](evidence/xlang3-cpp-lrpc-host-20261017.json).
