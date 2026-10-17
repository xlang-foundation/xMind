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
