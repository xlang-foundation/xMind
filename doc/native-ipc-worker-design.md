## Owned repository and skill tools — 2026-10-19

Explicit attached-folder repository-instruction calls previously built a primary-
folder invocation and returned access_denied. The actual AgentRunner regression
reproduces that failure against unchanged native production bytes.

AgentGuidanceToolHost now checks the live run, offered catalogue, membership
revision, selected folder, inherited denials, cancellation/deadline and duplicate
call identity. Instruction reads use the selected admitted folder and register
their scope for the following model request. Explicit discovery also survives a
skill activated earlier in the same tool batch. Skill catalogue/activation retain
their primary-folder contract and instruction budget; guidance grants no effects.

The complete native build and eight focused owner/agent/graph/grant/waiter
contracts pass in 23.17 seconds with all source, SDK, pure-library, dependency
and binary file sets/hashes unchanged. A first selector omitted the grants
contract: seven tests passed, but its eight-test wrapper correctly failed. That
receipt is retained; the final selector preflights all eight exact names.
This is local controlled model-fixture validation with actual files and xlang3
SQLite, not live provider or installed client acceptance. Current hosted, VS Code pane and worker IPC acceptance remain open. Guidance lifecycle
and trusted callable/discovery services still depend on the backend runner.
[Regression, final qualification and limits](evidence/guidance-tool-owner-20261019.json).

The complete local build and all 152 registered Release contracts subsequently
pass in 968.11 seconds with every source/SDK/library/dependency/binary set
and hash unchanged. Two actual Rust Analyzer contracts and the Cargo fixture
pass separately on this source with source, SDK and complete toolchain/shim
sets frozen; the earlier semantic failure remains unexplained. This source
is still uninstalled.
[Full current native qualification](evidence/guidance-tool-owner-full-20261019.json).

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
The complete local build and all 152 registered Release contracts now pass
in 964.83 seconds with all project, SDK header/runtime, pure-library, dependency
and binary file sets/hashes unchanged. The real LSP checks above are separate
from that default inventory. Source remains uninstalled; hosted/release
acceptance and the earlier unexplained semantic failure remain open.
[Complete local source qualification](evidence/language-server-read-owner-full-20261019.json).
[Exact source, outcomes and limits](evidence/language-server-read-owner-20261018.json).

Permissions and guidance apply to the requested document. Language-server
executables remain trusted and can index the workspace; read-only LSP methods
do not establish filesystem or network isolation. Registry admission/discovery,
instructions/skills, custom-callable ownership and worker transport remain
backend dependencies.

### Workspace read owner — 2026-10-18

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

 xlang3 IPC and native agent workers

Target integration for the [xMind product architecture](runtime-product-design.md).
The first registered-tool worker path is implemented; the broader agent-worker
scheduler and durable distributed execution described below remain pending.

## Current execution boundary — 2026-10-17

The native `AgentService` currently owns its C++ thread pool and constructs an
`AgentRunner` on those in-process workers. There is no per-agent placement
setting and no child-process execution of the model/tool loop. The existing
`xmind worker` entry point hosts registered trusted xlang3 callables over LRPC;
that path moves one tool implementation out of process, not an Agent or graph
run. Its passing worker contracts must not be counted as out-of-process agent
acceptance. The code references are
[`AgentService`](../Native/src/agent_service.cpp),
[`AgentRunner`](../Native/include/agentflow/agent_runner.hpp), and the
[`xlang3 tool worker`](../Native/src/agent_worker.cpp).

The source review confirms the ownership boundary that the implementation must
preserve: `AgentRunner` holds a direct `PersistenceService&`, while
`AgentService` owns root admission, cancellation and the durable run lifecycle.
Starting the current runner in another process would therefore create a second
SQLite writer and give that process direct effect authority. That is not the
worker architecture described here. The first implementation step must extract
a capability-scoped execution host for provider requests, tool discovery and
calls, approval waits, and durable event/run writes. An in-process host will
retain the current behavior; the worker host will carry only bounded,
versioned request/result values over authenticated xlang3 IPC. Provider keys,
SQLite handles, workspace roots and backend runtime objects stay with the
server owner. Only after both hosts pass the same AgentRunner contracts should
trusted definitions be allowed to select process placement.

The first code seam is extracted as `AgentRunJournal`. Direct run history,
event, budget, dynamic-plan, skill-selection and operation-journal calls from
`AgentRunner` use this owned-value interface; `PersistenceService` is its
current production implementation. Context snapshots and maintenance outcomes
now use the separate `AgentContextJournal`. `ContextManager` and `AgentRunner`
accept the two journals independently, while the SQLite-backed service remains
the current adapter for both. The real AgentRunner and context-engine contracts
exercise those overloads. Credential resolution deliberately stays on
`PersistenceService` and is excluded from the portable journals because it
returns secret bytes. This is still a persistence seam only: process execution,
workspace, MCP and A2A helpers and approval/effect authority remain with the
backend. The interfaces do not enable out-of-process AgentRunner execution. The
next extraction must route provider/tool and approval capabilities through the
host while keeping credentials and durable effects with the owner.

The model-call slice is now extracted as `AgentModelGateway`. `AgentRunner`
passes validated provider configuration, model request values and a stream
callback to the gateway; it no longer resolves a secret or calls a provider
transport itself. The local gateway captures only the saved credential
reference, resolves its bytes inside the backend, and handles normal inference,
Responses context counting and compaction. The context-engine and real
AgentRunner provider/tool contracts pass through this gateway. This keeps API
keys out of its call values and creates an injectable provider boundary, but
does not yet implement an IPC-backed gateway or move workspace/MCP/A2A tools,
approval waits or the full AgentRunner loop into a worker.

`RootExecutionBudget` now uses `AgentRunJournal` for durable root admission,
model-call reservation and completion, dynamic-plan admission, continuation,
and human-pause suspension. It no longer needs the concrete
`PersistenceService`; the journal carries owned request/result values and the
current SQLite adapter remains on the backend. The focused Release contracts
for the real AgentRunner, dynamic-plan engine and repository, context engine,
GraphRunner, and A2A agent registry all pass against this boundary. The exact
local invocation and limits are recorded in
[`agent-run-budget-journal-local-20261017.json`](evidence/agent-run-budget-journal-local-20261017.json).
This narrows the persistence dependency for eventual worker hosting; it does
not move the runner, workspace tools or effect authority into another process.

Permission waits now also depend on `AgentRunJournal`: proposing an operation,
reading its durable decision, claiming it once, and recording denial, expiry or
cancellation no longer require the waiter to hold `PersistenceService`. The
agent-runner contract exercises the real approval path, and the dedicated
waiter contract covers durable decisions, cancellation, expiry and one-shot
claims. Evidence is recorded in
[`agent-approval-journal-local-20261017.json`](evidence/agent-approval-journal-local-20261017.json).
Permission grant lookup still uses the local persistence adapter, and effect
executors still receive backend persistence directly; approvals and effect
dispatch have not yet become a worker RPC.

The file-effect slice now uses `AgentFileToolHost`: the run loop sends owned
identity, membership and argument values. The local owner performs folder
resolution, guidance checks, inherited-denial checks, preparation, controller
approval and edit/create/patch/restore execution. Prepared filesystem plans,
instruction verifiers, expiry and operation IDs stay in the backend. The owner
checks its frozen catalogue and active run and rejects duplicate call identity
within its lifetime. This is not an authenticated or durable worker RPC; the
current owner is constructed inside the in-process execution path. Remaining
admission, instruction, MCP/A2A and other concrete dependencies still prevent a
separate AgentRunner process. See [placement status](agent-process-placement.md).

The remaining product work is a placement-aware execution path for the same
agent/model/tool loop while preserving the parent backend's run identity,
SQLite persistence, permissions, approvals, event stream and effect journal.
The acceptance section below is required before that option can be advertised.

## Source-driven next boundary — 2026-10-18

The process-tool slice now follows the same owner pattern as file and remote
tools: `AgentProcessOwnerHost` accepts owned run/attempt/call identity,
workspace/folder binding, tool and argument values. The backend owns operation
identity/expiry, offered-catalogue checks, inherited denials, instruction
preconditions, approval and effect dispatch. Uncertain wait and pending stop
results remain unsuccessful instead of being overwritten by the loop's general
guidance-success assignment. Its actual approved attached-folder xlang3 command
and SQLite reopen pass; recording-adapter result probes are labelled separately.
The complete current local native suite passes 151/151 in 909.5 seconds with
the full frozen input/runtime/binary closure unchanged.
[Qualification and retained failures](evidence/process-tool-owner-20261018.json).

The lower-level `AgentProcessToolHost` still exposes trusted profile bindings to
backend admission checks and owns background joins. The new owner adapter is
not a worker protocol, durable call ledger or placement setting. The remaining
tool/admission/instruction dependencies described below still need extraction
before the same AgentRunner can execute in a separate process.

The earlier baseline passed **147/147** contracts, including
the current `AgentRunner`, graph, local-profile, MCP SDK and backend-owner
contracts ([exact record](evidence/native-current-source-full-20261017.json)).
That establishes the in-process baseline; it does not change the worker boundary.

The first workspace slice now uses `AgentWorkspaceToolHost`: `WorkspaceTools`
and `WorkspaceToolSet` implement its catalogue and bounded JSON invocation
methods, and `AgentRunner` consumes them through that interface. The single-root
and multi-folder paths passed the focused AgentRunner, graph, workspace and
graph-HTTP contracts ([test record](evidence/agent-workspace-tool-host-local-20261017.json)).
This capability remains in-process and owns the local workspace handles; it
does not yet cross LRPC or mediate mutations.

The next extraction remains a run-scoped `AgentToolHost` covering the complete
model-visible catalogue and dispatch. File, remote and process dispatch now use
owner adapters, but the runner still constructs these with concrete backend
capabilities. Process-profile checks/background joins, MCP/A2A discovery,
trusted xlang3 callables, permission grants and instruction contexts still
depend on the backend inside the execution path.
`AgentRunJournal`, `AgentContextJournal` and `AgentModelGateway` isolate durable
run/context records and model requests, but they do not mediate those effects.

The host's public call should carry a versioned run capability, attempt and
call IDs, the sealed catalogue revision, tool name, bounded argument JSON,
workspace ID/revision and optional folder ID, plus a deadline. It must not
carry a workspace root, executable path, credential reference or database
handle. The backend resolves folder IDs from the admitted membership, rechecks
the sealed tool binding and agent policy, and invokes the existing native tool
implementation. Approval waiting, grant lookup, effect claims, MCP/A2A receipts
and event persistence stay on the backend. Progress events are committed by the
backend before forwarding to either the in-process runner or a worker. A lost
reply after dispatch is recorded as uncertain and is never transparently
replayed.

Implementation order is therefore: extend the workspace capability to the
complete catalogue and tool-call switch behind an in-process `AgentToolHost`
without changing behavior; run the same AgentRunner contracts through that
host; add the authenticated xlang3 LRPC adapter with attempt/call idempotency
and bounded streaming; then run those contracts in a child process and add
crash/recovery acceptance. No agent process-placement setting is valid until
the remote host implements the same effect and approval contract.

## Verified SDK foundation

The existing pinned xlang3 SDK revision
`f0c425cff5b6bf47f18144b31e4dc1ac16c9a9f7` exposes native C++
`X::Runtime::ImportRemote(name, endpoint)` and the C API
`x3_runtime_probe_remote(runtime, endpoint, timeout_ms, info)`.
Its existing shared-memory endpoint scheme is `lrpc:`. The script-side APIs
`register_remote_object` and `lrpc_listen` register and serve remote objects.
These are actual SDK APIs, not proposed xMind command names.

The existing official smoke script was executed with the pinned xlang3
executable and native C++ IPC client. The script checked its expected output,
four concurrent script clients and the native client. The native client tests
remote calls, returned objects/callables, nested values and binary transfers,
including rejection and subsequent recovery at the transport payload limit.
The three executed binary hashes were unchanged. No SDK rebuild, CPython
execution, primary xlang3 checkout mutation or throughput benchmark occurred.
[Scoped local evidence](evidence/xlang3-ipc-foundation-local.json).

## xMind-selected runtime validation — 2026-10-13

The official `tests/ipc/run_lrpc_smoke.ps1` was rerun against the exact
`build-pytest-pathaware/Release` xlang3 runtime selected in the xMind native
CMake cache. The script client, four concurrent script clients, and native
C++ `ImportRemote` client all passed, including nested values, returned
objects/callables, binary transfers, payload-limit rejection and recovery.
The xlang3 runtime DLL copied beside the xMind native build has the same
SHA-256 as the tested DLL. The complete sanitized invocation and binary hashes
are in [selected-runtime evidence](evidence/xlang3-ipc-selected-runtime-20261013.json).
This closes the exact-runtime transport smoke gap only; xMind worker identity,
authorization, agent dispatch and failure reconciliation remain unimplemented.

Further failure, ownership and authorization acceptance must be performed
against the exact runtime selected for xMind before production IPC admission.
The existing probe returns endpoint process identity; endpoint discovery alone
does not authenticate an xMind worker or grant a workspace capability.

### Native C++ host API extension — 2026-10-17

The xlang3 mainline SDK baseline above is historical: it includes the LRPC
client and probe but has no supported C++ service-registration/listen API.
The xMind-specific host extension was introduced at
`ad7e74058ebdc941ec656c315a8a47a01c12d74e`. The current xMind Windows native
CI pin is descendant `b0da71f22e9591b6bd99883b21c50ad85dbe4bed` on
[branch `xmind-fastapi-pydantic-compat`](https://github.com/CantorAI/xlang3/tree/xmind-fastapi-pydantic-compat).
It retains `X::Runtime::RegisterRemoteObject` and `X::Runtime::LrpcListen` and
adds pydantic-core distribution metadata for xlang3 pip. The exact hosted gate
for the current pin is queued at
[run 39083212989](https://github.com/xlang-foundation/xMind/actions/runs/39083212989).
The original host contract's local build and smoke evidence remains in
[xlang3 C++ host evidence](evidence/xlang3-cpp-lrpc-host-20261017.json).
This provides a C++-owned xlang3 LRPC endpoint; backend capability RPC,
per-run authentication and AgentRunner process placement remain unfinished.

An isolated xMind Release build at source `cab94b6` now links against that
exact xlang3 branch runtime. `native_agent_runner_contract` passed (1/1), and
`native_xlang3_shared_memory_worker_contract` passed (1/1) against the same
runtime. This verifies existing AgentRunner contracts and the existing
registered-xlang3-tool worker still work with the SDK extension; it does not
test process-bound AgentRunner execution or make the xlang3 branch a release
dependency. [Exact revisions, hashes and results](evidence/xmind-xlang3-cpp-host-integration-20261017.json).

## Integration boundary

The backend launches its own `xmind worker` process and embeds xlang3 there.
A worker bootstraps a narrowly scoped registered endpoint. The backend pins
worker process identity, connection/workspace binding, protocol revision and
an expiring capability before dispatch. A client-supplied endpoint name cannot
select arbitrary imported objects or gain approval authority.

Use owned serialized command/result values with bounded schemas and sizes.
Do not transfer backend runtime handles, SQLite handles, credentials or
arbitrary remote-object proxies as public AgentFlow state. The same logical
commands/events should be usable over HTTP or local IPC; changing transport
must not change permissions, effect receipts or run identity.

The scheduler assigns a durable attempt and lease. The worker supplies bounded
progress/result messages; the backend validates and commits events before
views observe them. Registered platform effects are requested through backend
tools and retain the normal per-effect approvals/journal. Workers cannot
complete a run by directly writing a database or declaring a synthetic receipt.

Cancellation, heartbeat/lease expiry, worker death and ambiguous results need
explicit states. Unknown tool effects must be reconciled rather than repeated
automatically. Reconnection resumes the recorded cursor/attempt; it cannot
create a second execution of already admitted work.

## Acceptance required before delivery

- Run the same agent/tool contract in process and in a real xMind worker, with
  actual gateway responses, effects and separately recorded usage.
- Validate identity/authentication, bounded framing, cancellation and deadlines;
  reject stale leases, foreign workspaces, malformed messages and schema drift.
- Exercise worker crash before dispatch, during inference and around an approved
  effect; preserve recorded outcomes and reconcile uncertainty without replay.
- Verify multiple workers, queue/resource limits, backpressure and concurrent
  UI observation without transferring xlang3 values between runtime owners.
- Verify backend restart and reconnect against xlang3 SQLite; preserve the
  selected profile and expose failures rather than silently switching transport.

A worker process provides process/crash isolation. Arbitrary script filesystem
and network restrictions require an implemented OS sandbox. Shared-memory IPC
does not itself provide that sandbox, and no sandbox guarantee is claimed.

## Implemented local tool-worker slice — 2026-10-13

Trusted xlang3 tool definitions accept `execution: in_process|worker` in the
existing startup YAML and SQLite snapshot; omitted placement stays
`in_process`. The single `xmind` executable hosts the private `worker` entry
point and packages its xlang3 service script beside the binary. For `worker`,
the backend starts a child on the first call for a trusted tool binding and
reuses that child for later calls. It probes the random `lrpc:` endpoint with
the selected xlang3 runtime, verifies that the endpoint reports the child PID,
and transfers only bounded JSON strings. The child runs the trusted
module/function binding and the backend owns the model loop and persisted tool
result. Calls to one worker are serialized. Cancellation retires the child; a
later call can start a replacement. The worker starts with a minimal Windows
environment so provider credentials held in backend environment variables are
not copied into it.

The native contract uses an actual xlang3 fixture and unified `xmind` child,
checks repeated and concurrent call behavior through IPC, then cancels a slow
callable and checks prompt child retirement. A second contract sends the tool
call through the real AgentRunner/model protocol fixture and verifies the
persisted continuation.
The parent verifies that the LRPC endpoint belongs to the spawned PID. It now
also gives each trusted-tool worker a fresh 256-bit capability through a
restricted inherited pipe; the secret is absent from command-line arguments
and environment variables. Each invocation carries an HMAC-SHA256 over its
monotonic sequence number and bounded JSON arguments. The worker rejects
missing, invalid, and replayed requests before calling the bound function. A
startup probe attempts an unauthenticated call, completes a signed handshake,
and replays that handshake to confirm rejection before the normal contract
proceeds. This protects the service from other callers that can find
the local endpoint but cannot access the parent-held capability. It remains a
trusted-tool worker contract, not process-bound AgentRunner execution.
Durable leases, AgentRunner-scoped capabilities, restart reconciliation, and
OS sandboxing remain unimplemented; no completion or isolation guarantee for
those features is implied.

## Reusable trusted-tool workers — 2026-10-15

The native xlang3 callable bridge now keeps a worker process for each exact
trusted binding (module, function and code/runtime roots), serializes calls to
that worker, and reuses its imported callable across successive invocations.
The registry is capped at 16 sessions; when full it retires an idle session,
and reports capacity exhaustion if every session is in use. Cancellation or an
IPC/result failure retires the worker. xMind never retries a dispatched call
after transport failure, since the callable may already have produced an
effect. A later invocation may start a replacement worker. The Release
`native_xlang3_shared_memory_worker_contract` verifies repeated calls observe
the same worker-local module state and cancellation retires a slow worker; the
actual AgentRunner contract verifies tool-result continuation through the
reused-worker path. This is an in-process scheduler-owned local pool only:
leases, durable attempt journaling/reconciliation, worker identity beyond the
spawned PID, remote distribution, and OS sandboxing remain open.

### Local LRPC capability authentication — 2026-10-17

The Windows parent now creates a 256-bit secret per worker session and sends it
once over an anonymous pipe whose only inherited handles are the worker's
stdin and NUL output handles. The worker keeps the secret in memory. Each call
contains a strictly increasing sequence and an HMAC-SHA256 over that sequence
and the exact bounded JSON argument string; the worker compares the tag in
constant time and consumes each sequence once. The key is never in process
arguments, environment variables, or the LRPC payload. Worker startup checks
that unauthenticated and replayed requests are rejected before reporting the
child ready.
The Release worker contract then verifies authenticated repeated and concurrent
calls, actual xlang3 execution, and cancellation. This capability belongs to a
reusable trusted-tool worker session; it does not yet provide the fresh
per-AgentRun capability or shared-backend authorization required by the
AgentRunner process-placement design. [Build and test evidence](evidence/xmind-worker-capability-auth-20261017.json).
