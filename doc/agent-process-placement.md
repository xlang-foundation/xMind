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

1. Add a backend-owned execution-placement enum and revision-bound config for
   primary agents, subagents and graph agent nodes. Reject unknown values and
   preserve the default for existing records.
2. Add a native worker supervisor and private IPC protocol. Keep run admission,
   grants, credentials, durable journal ownership and recovery on the same
   backend profile. Bind every request and reply to the run, profile revision,
   worker identity and protocol version.
3. Route only opted-in runs to the worker. The worker executes the same
   `AgentRunner`; clients use the existing HTTP/SSE and CLI interfaces.
4. Test in-process and process execution against the same provider and tool
   fixtures. Verify actual event/history writes, workspace reads, approved
   edits, model/tool continuations, provider metrics, graph child results and
   process identity.
5. Exercise cancellation during provider streaming, tool execution and human
   pause; worker startup failure; crash before and after a durable effect;
   lost replies; server shutdown; and restart/reconciliation. Prove completed
   tools are never replayed and unresolved effects remain quarantined.
6. Accept through the installed webpage and VS Code clients, and verify the
   same process-bound run remains inspectable and controllable after either
   view reconnects.

No process-placement config, C++ AgentRunner worker, or acceptance for this
contract is implemented yet. This document records the requested direction and
the testable boundary; it is not a completion claim.
