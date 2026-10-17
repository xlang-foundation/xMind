# xlang3 IPC and native agent workers

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

The first code seam is now extracted as `AgentRunJournal`. Direct run history,
event, budget, dynamic-plan, skill-selection and operation-journal calls from
`AgentRunner` use this owned-value interface; `PersistenceService` is its
current production implementation. The runner also accepts the journal
separately from its backend services, and the real AgentRunner contract passes
through that constructor. Release contracts for AgentService, GraphRunner and
the A2A agent registry passed against the rebuilt libraries. This is a journal
boundary only: process execution, context, workspace, MCP and A2A helper objects
still receive concrete backend services. Credential resolution deliberately
stays on `PersistenceService` and is excluded from the portable journal because
it returns secret bytes. The interface does not enable out-of-process
AgentRunner execution. The next extraction must route provider/tool and
approval capabilities through the host while keeping credentials and durable
effects with the owner.

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

The remaining product work is a placement-aware execution path for the same
agent/model/tool loop while preserving the parent backend's run identity,
SQLite persistence, permissions, approvals, event stream and effect journal.
The acceptance section below is required before that option can be advertised.

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
The PID check binds discovery to the spawned process but is not an
authentication boundary against another process running as the same user.
Durable leases, scoped capabilities, restart reconciliation, and OS sandboxing
remain unimplemented; no completion or isolation guarantee for those features
is implied.

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
