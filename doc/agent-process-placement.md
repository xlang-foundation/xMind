# Agent execution placement

## Owned language-server queries — 2026-10-18

Configured model language-server calls now cross the existing owned read-tool
interface. The backend checks active run, offered catalogue, admitted revision
and folder, requested-document guidance, and selected/inherited permissions.
Ask grants retain session/folder/policy/tool/path binding and durable receipts;
changed guidance after approval withholds dispatch. A custom xlang3 tool named
`language_server` still runs when no native registry is configured.

Eight focused owner contracts pass. Two actual Rust Analyzer contracts and
the Cargo fixture pass, including AgentRunner queries/denial and the actual
xlang3 alias through an explicit model-gateway fixture. All source, SDK, pure
library, dependency and binary sets remain frozen, including Rust bin/lib,
libexec and four tool shims. This is not live model inference or worker IPC.
The first semantic definition lookup failed before the new AgentRunner case;
its cause is unexplained despite later diagnostic and final fresh-fixture
passes. Fixture compile and wildcard-input failures remain separately recorded.
This source is uninstalled; complete native and hosted gates remain open.
[Exact source, outcomes and limits](evidence/language-server-read-owner-20261018.json).

Permissions and guidance apply to the requested document. Language-server
executables remain trusted and can index the workspace; read-only LSP methods
do not establish filesystem or network isolation. Registry admission/discovery,
instructions/skills, custom-callable ownership and worker transport remain
backend dependencies.

## Workspace read owner — 2026-10-18

`AgentReadToolHost` now owns the five model-visible workspace read tools through
the existing versioned owned workspace envelope. The backend validates active
run/catalogue/membership, retains inherited denials, generates approval identity
and expiry, and resolves session/folder/policy-bound grants. It rechecks guidance
after an approval wait; changed guidance retires the claimed read without
revealing file contents. Actual primary/attached reads, approvals, grant reuse
and isolation, directory denial, cancellation and SQLite reopen pass alongside
the real AgentRunner and graph contracts: eight focused tests in 23.27 seconds.
The retained fixture compile failure used an incorrect field name; production
code stayed unchanged across that correction.
[Focused proof](evidence/read-tool-owner-focused-20261018.json).

This native source is installed in previews 0.1.32 and 0.1.33. Its complete build and all 152 local Release
contracts pass in 903.83 seconds, with 2,617 project inputs, 16 SDK files, 76
SDK runtime/import-library files, 1,870 pure libraries, 18,598 dependencies and
127 binaries frozen. [Full qualification](evidence/read-tool-owner-full-20261018.json).
It is a local owner with a lifetime duplicate guard. Complete tool discovery,
instructions/skills, language servers, trusted callables, delegation, process
admission/background joins and authenticated/durable worker transport remain
backend dependencies. Process placement is not implemented. Preview 0.1.33
contains the corrected shared client; rendered headless delegated execution,
package verification and their limits are recorded in
[coding-package-0.1.33](evidence/coding-package-0.1.33-20261018.json).

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

## Process-tool owner boundary — 2026-10-18

The run loop now invokes `run_process`, `wait_process` and `stop_process`
through `AgentProcessOwnerHost`. Its owned request carries run/attempt/call
identity, workspace revision, folder, tool and bounded JSON arguments. The
backend chooses operation IDs and expiry, resolves the admitted folder and
repository/skill guidance, checks the active run and offered catalogue, retains
inherited denials and rejects duplicate call identity before approval can wait.
The lower-level executor keeps trusted executable/profile bindings, approval,
effect claims, output artifacts and process lifecycle ownership.

Uncertain waits and unconfirmed stop requests now retain an unsuccessful tool
result. Previously a later assignment in the run loop overwrote that state.
The new contract checks those values with an explicit recording adapter;
it separately runs an actual approved xlang3 command in an attached folder and
reopens the xlang3-backed SQLite receipt without replay.

All five focused contracts pass in 20.79 seconds. After building every native
target, the complete local suite passes 151/151 in 909.5 seconds with frozen
project inputs, SDK headers/runtime/import library, pure libraries, dependencies
and 126 binaries unchanged. The initial fixture expected LF bytes after a
Windows text-mode write; its retained diagnostic proves actual exit zero and
correct-folder CRLF output. The fixture now writes exact bytes with the original
byte assertion; production owner/runner code stayed unchanged across that fix.
[Exact source patch and evidence](evidence/process-tool-owner-20261018.json).

This remains an in-process owner adapter with a lifetime call guard. Process
profile validation, background joins, admission and other backend dependencies
remain in the runner/service path. Authenticated IPC, durable RPC deduplication,
worker supervision and actual process-bound AgentRunner execution remain open.
Installed 0.1.31 includes the process-tool owner and passes a real saved-key
Claude coding task; the later read-tool owner is installed in 0.1.32/0.1.33.

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
   envelope. File-tool preparation and effects now cross `AgentFileToolHost`;
   Remote MCP/A2A invocation now crosses AgentRemoteToolHost; discovery and other backend services still need owner capability boundaries.
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

### Remote-tool owner boundary — installed 0.1.26

AgentRunner now invokes native MCP and A2A tools through AgentRemoteToolHost.
Its request contains owned run/attempt/call identity, workspace revision, tool
alias, arguments and a bounded activity index. The backend owns the registries,
filtered offered catalogue, permission layers, root instruction/skill context,
deadline, operation IDs, approval expiry and A2A event journal. A request cannot
supply a peer endpoint, credential, approval actor or instruction verifier.
Catalogue refresh remains owner-controlled between model turns. The host keeps
a lifetime guard against re-entering the same attempt/call identity, including
while approval is pending; this is not durable worker RPC admission.

Five focused native contracts pass in 25.44 seconds with all 2,607 project
inputs, libraries, SDK/runtime, 125 binaries and 18,598 dependencies unchanged.
The new contract checks actual native approval/guidance/effect behavior and
xlang3 SQLite reopen against an explicit synthetic MCP client; existing A2A
AgentRunner and dynamic MCP/agent/file contracts pass through the new path.
The initial fixture compilation failure is retained separately.
[Focused evidence](evidence/remote-tool-owner-20261018.json).

All 150 local Release contracts now pass in 1,001.22 seconds with the complete
source, SDK, library, dependency and binary file sets/hashes unchanged.
The installed 0.1.26 build also passes a real saved-key Claude task: one README
read through the official external MCP filesystem server, one approved native
file creation and one actual xlang3 verification process with exit code zero.
The peer's 4,078 input files remain unchanged. This uses the public operator
serve path and a Node browser-controller fixture; managed-profile MCP setup,
rendered client acceptance, hosted acceptance and process-bound AgentRunner
execution remain open. The older user page remains running on 0.1.25.
[Installed scope and receipts](evidence/coding-package-0.1.26-20261018.json).

### File-tool owner boundary — current source

The shared AgentRunner sends owned run/attempt/call identity, workspace revision,
folder ID, tool name and JSON arguments to `AgentFileToolHost`. Its local owner
keeps workspace handles, instruction contexts, prepared patches, permissions,
approval expiry, operation IDs and the xlang3-backed persistence service. It
prepares and executes edit, create, single/multi-folder patch and restore calls.
The invocation cannot supply a root path, native object, prepared plan or grant.
The owner rejects foreign or retired runs, changed membership bindings, tools
outside its frozen catalogue, inherited denials and a repeated call identity
within its lifetime. Undelivered guidance returns the existing model-visible
request for a fresh model turn without an approval proposal. Actual partial
patch outcomes and uncertain-effect failures retain the existing executors.

This is an in-process owner boundary. Its policy and folder/instruction
capabilities are constructed by trusted backend code and must outlive it.
AgentRunner still requires concrete backend services for admission, instruction
delivery and MCP/A2A. The local call guard is not durable RPC deduplication,
worker authentication or crash recovery. Process placement must remain
unavailable until a complete owner host, authenticated transport, supervisor and
restart/reconciliation contracts execute the same agent loop. Current-source
build and native validation are tracked separately from installed 0.1.21.

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

The process host now receives a versioned `AgentProcessInvocation` value with
operation/run IDs, workspace ID and revision, selected folder ID, arguments,
expiry and background permission. It resolves folder roots from the durable
run-admission snapshot and rejects mismatched revisions, folders, empty IDs
and expired requests. The host no longer accepts a caller-owned
`WorkspaceToolSet` pointer.

The call still carries a local stop token and native instruction-verification
callback. It is not an authenticated IPC message, does not verify replay or
active-run authorization, and does not allow remote-host injection or execute
`AgentRunner` out of process. MCP/A2A, approval and other workspace-effect
dispatch also remain in the concrete runner. This is a local value boundary,
not process-placement acceptance.

The Release process-executor, AgentRunner and graph-runner contracts pass with
the typed process request, including attached-folder dispatch, revision and
folder mismatch rejection, request expiry and both caller paths. A follow-up
regression test reproduced and corrected root-run ancestry termination: a
single-folder run without a membership event must use the verified primary
workspace rather than fail the ancestry depth check. The process-executor,
AgentRunner and graph-runner Release contracts then passed 3/3 in 27.95 seconds.

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
at `ad7e74058ebdc941ec656c315a8a47a01c12d74e`. At that checkpoint, xMind pinned descendant
`b0da71f22e9591b6bd99883b21c50ad85dbe4bed` from branch
[`xmind-fastapi-pydantic-compat`](https://github.com/CantorAI/xlang3/tree/xmind-fastapi-pydantic-compat);
it retains the LRPC host API and adds pydantic-core package metadata required
by xlang3 pip. The native host/second-runtime contract passed locally at the
original host revision. The hosted Release gate for that pin was queued
at [run 39083212989](https://github.com/xlang-foundation/xMind/actions/runs/39083212989).
This establishes neither process-bound AgentRunner execution nor its
per-run authentication, durable recovery or client acceptance. [Original host
build and test evidence](evidence/xlang3-cpp-lrpc-host-20261017.json).
