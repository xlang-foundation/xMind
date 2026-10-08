# Native xMind Server and console client

This guide retains historical server, watcher and approval component checkpoints.
Their test counts, preview descriptions and pending capabilities apply to their
recorded revisions. Current provider enrollment, interactive CLI, graphs,
delegation, live browser acceptance and remaining limits are recorded in
[validation status](VALIDATION_STATUS.md), [interactive CLI](native-interactive-cli.md)
and [Responses continuation](native-responses-reasoning.md). Team authorization,
PostgreSQL, WebRTC and Electron belong to private Nexus.

Historical complete baseline: source `3297a4bce2d591c0f1c1dd37ccc18a15297369e7`, [35 native contracts](evidence/native-process-passing-ci-ctest.log) and [40 extension contracts in hosted TAP](evidence/native-process-passing-ci-job.log). Approved actual edits/creation, registered MCP tools, executable-bound foreground commands, durable retained output and native watcher reconnect passed with synthetic inference and actual independent child/file/database effects. That checkpoint's unseeded sidebar used the tested bundle without a configured model. See [process scope and remaining limits](native-process-tools.md).

The native CLI now prepares `xmind_cli PORT watch RUN_ID [AFTER]` for continuous observation through the same authenticated backend. It prints one persisted event per line as NDJSON, flushes each record, advances only to its actual `seq`, and polls every 250 ms. Reconnect with the last emitted sequence; `process.output` fragments retain their exact hexadecimal bytes and channel offsets. JSON escapes control characters instead of executing terminal output. The watcher reads a final event tail after observing an actual terminal state, exits 0 for completed, 2 for cancelled and 1 for failure/observation error. It does not launch, approve or cancel work; closing this client leaves execution owned by the backend. Transport errors stop observation rather than restarting a run.

Actual server/CLI checks passed observing an awaiting approval, disconnect without backend cancellation, cursor reconnect through real process output and completion, terminal cancellation, invalid cursor/authentication/missing run, and model-free restart replay. The tested watcher executable is included in the new preview bundle. Interactive console approval/TUI, background jobs and broader parity remain required.

Verified inspection checkpoint: [CI run 37600409106](https://github.com/xlang-foundation/xMind/actions/runs/37600409106) passed all nineteen native contracts and twenty-three extension contracts; [complete CTest evidence](evidence/native-uncertain-inspection-ci-ctest.log). When the backend has a configured workspace, authenticated `GET /v1/operations/OPERATION_ID/inspection` and CLI `inspect-edit OPERATION_ID` call the existing native uncertain-edit inspector. Only recorded uncertain `replace_file` operations with a retired owner and matching opened workspace identity are inspected. Clients cannot supply a file path or mutation request. The response includes recorded operation, observed file identity/hash/size, observation time, `same_file`, and `match` (`before`, `after`, `different`), with `quarantine_released:false`. No observed bytes or inferred attribution are supplied. The read leaves the operation and events unchanged and never grants, retries, restores or releases an effect. File inspection failure does not imply that no effect occurred. The HTTP/CLI fixture explicitly constructs uncertain journal state and externally writes observed fixture bytes; the separate executor contract establishes an actual post-effect journal fault and restart. Neither establishes a process kill during a partial write.

For inspection without a model/provider credential, select `--inspection-workspace DIR` on the native server, or `-InspectionWorkspace DIR` on `Tools/agentflow.ps1 -Action Serve`. It opens the trusted server-side workspace for inspection only and leaves model execution disabled when no model is configured. It is mutually exclusive with `--workspace`; a configured execution workspace already supplies the inspection boundary. The same database and unchanged workspace directory identity are required after restart. Changing the root spelling cannot authorize inspecting another workspace. The contract reopens the actual production server and checks raw file hash, unchanged journal, absent model execution and wrong-workspace rejection.

The C++ server exposes durable sessions and schedules the native model/tool engine through `AgentService`. The console and editor host clients are separate HTTP observers. Runtime objects and database files remain owned by the server, with HTTP transport isolated from the agent core through `RunExecutor`.

The Release build and eighteen native contracts passed, including configured run admission/cancellation through HTTP/CLI, the actual editor host client, the operation journal, permission waits and approved real file edits through the local approval API. Inference peers in those contracts are synthetic; filesystem operations and embedded-xlang3 storage are real. Live inference, actual editor UI behavior and complete coding tasks remain unverified.

## Current API

| Method | Path | Behavior |
| --- | --- | --- |
| GET | /v1/health | Reports API, core/storage and execution capability |
| GET | /v1/mcp/servers | Saved stdio server ID/revision/enabled metadata; connections are per run, secrets and command configuration stay private |
| GET / POST | /v1/sessions | List/create durable sessions |
| GET | /v1/sessions/{id}/history | Read conversation history |
| POST | /v1/sessions/{id}/messages | Persist `{role:"user",data:{content:"nonempty text"}}`; rejects other roles, fields, NUL and non-text content |
| GET | /v1/sessions/{id}/runs | Read persisted run history |
| GET | /v1/runs/{id} | Read persisted run state |
| GET | /v1/runs/{id}/events?after=N | Read ordered events after a validated cursor |
| GET | /v1/runs/{id}/operations | Read durable effect proposals/outcomes for this run |
| GET | /v1/operations/{id} | Read exact proposal argument bytes and durable state/outcome |
| POST | /v1/operations/{id}/decision | Authenticated local owner allows/denies an immutable proposal using `{decision:"allow"}` or `{decision:"deny"}` |
| POST | /v1/runs | With a configured engine: admit `{session_id,prompt,id?}` and return 202 with queued admission snapshot |
| POST | /v1/runs/{id}/cancel | With a configured engine: accept `{}` and request cancellation; inspect state/events for its outcome |

No public route manually advances run state or inserts assistant/tool messages. Run lifecycle belongs to the native engine. Without a model configuration, execution routes are absent and health reports `agent_execution: false`. With a model configuration, `agent_execution: true` reports that the engine is installed; it does not certify endpoint reachability or successful inference. An executor fault reports degraded health and rejects new work.

Admission has a bounded pending queue (default 128, maximum 4096), serviced by native workers (default 2, maximum 16). `--workers` and `--queue-limit` select these bounds. Queue saturation returns 503 before persisting a prompt/run; only one active root is allowed per session. Running cancellation interrupts the real engine's stop token; queued cancellation persists its outcome and frees capacity. A 202 cancellation response does not claim that external work has already stopped. Graceful executor shutdown cancels and joins active/queued work before persistence teardown. Crash recovery marks interrupted queued/running records failed; it never creates an answer.

## Local operation

Build using `Tools/native-milestone.ps1 -Action Build`. Set `XMIND_AUTH_TOKEN` privately in the environment of server and client to the same random 32-256 printable-byte value, with no spaces. It is not a provider credential. Neither executable prints it or accepts it through command arguments. Then:

```powershell
.\build\native\Release\xmind_server.exe --db .agentflow\server.sqlite --modules ..\xlang3\build\Release\modules --stdlib C:\Python\Python314\Lib --port 8765
# In another console with the same authentication environment:
.\build\native\Release\xmind_cli.exe 8765 health
.\build\native\Release\xmind_cli.exe 8765 create-session "My project"
.\build\native\Release\xmind_cli.exe 8765 sessions
.\build\native\Release\xmind_cli.exe 8765 append-message SESSION_ID "My message"
.\build\native\Release\xmind_cli.exe 8765 history SESSION_ID
```

Create the database parent directory first. The server binds only to 127.0.0.1; its configured Host header and bearer token are required. Browser Origin requests are denied until an authorized view adapter is configured. Errors are JSON and backend-internal database diagnostics are not returned. Windows Ctrl+C stops HTTP admission and drains the persistence service. Forced termination uses existing startup interruption recovery on next ownership acquisition.

The loopback token authenticates one full-access backend principal, `local-owner`, including inspection and decisions for all locally stored proposals. The server supplies that actor; requests cannot specify one. This is local-owner authorization, not team user identity or project isolation. Shared deployment requires scoped users/controllers/workers before exposing these routes remotely.

For an existing backend-owned proposal, inspect its exact `arguments_json` before deciding:

```powershell
.\build\native\Release\xmind_cli.exe 8765 operations RUN_ID
.\build\native\Release\xmind_cli.exe 8765 operation OPERATION_ID
.\build\native\Release\xmind_cli.exe 8765 decide OPERATION_ID allow
# Or: decide OPERATION_ID deny
.\build\native\Release\xmind_cli.exe 8765 operation OPERATION_ID
```

Operation responses retain argument/result JSON as strings to preserve exact large-number/content bytes. Clients cannot replace a proposal payload or change its actor; duplicate JSON request keys are rejected. IDs are never reused. A decision response records the grant/denial, not effect completion; inspect the later outcome. There are no public proposal creation, effect claim or outcome mutation routes. The native agent still offers only read tools, so it does not yet create coding edit proposals. The approved-effect HTTP contract submits proposals through a test driver using the actual backend executor, not a model or a fabricated product route.

For actual execution, select a chat-completions streaming endpoint/model explicitly:

```powershell
.\Tools\agentflow.ps1 -Action Serve -Model MODEL_ID -ModelEndpoint CHAT_COMPLETIONS_URL -ModelTools supported -Workspace WORKSPACE_DIR
.\Tools\agentflow.ps1 -Action Client -Port 8765 run SESSION_ID 'Read the project README'
.\Tools\agentflow.ps1 -Action Client -Port 8765 status RUN_ID
.\Tools\agentflow.ps1 -Action Client -Port 8765 events RUN_ID
.\Tools\agentflow.ps1 -Action Client -Port 8765 cancel RUN_ID
```

`--model` and `--model-endpoint` are required together. Workspace tools require an explicit `--model-tools supported` declaration; capability defaults to unknown. The three available tools read/list/search within the backend workspace. Omitting the workspace configures a text-only agent. Native provider transport currently requires Windows. HTTPS uses OS certificate validation; plain HTTP is allowed only for loopback endpoints.

Configure a provider key privately with `XMIND_API_KEY` in the server process environment. The backend protects and persists it through the credential repository and clears its inherited environment entry after copying it into owned secret bytes. The printed credential reference contains no key. Pass `-CredentialId ID` to reuse that encrypted credential on later launches without the environment key. The credential purpose binds to a hash of the exact configured endpoint. The user-bound Windows protection currently supports local use, not a company-wide shared vault. An endpoint requiring no key can also run without a credential reference. A real provider error produces a failed run with redacted diagnostics.

## Evidence and remaining delivery

`native_http_cli_contract` runs independent Node HTTP peers and the compiled C++ console client against the compiled server. It verifies auth/Host/Origin rejection, duplicate Authorization headers, input validation, duplicate-session conflict, concurrent persisted user messages, independent client visibility and session/message survival after process termination/restart. It also checks that product execution/transition routes are absent and clients cannot insert fabricated assistant messages. Evidence: [native-http-cli-ctest.log](evidence/native-http-cli-ctest.log). The subsequent test also invoked the native PowerShell launcher against the same server: [native-launcher-http-ctest.log](evidence/native-launcher-http-ctest.log).

Current evidence: [native-execution-server-ctest.log](evidence/native-execution-server-ctest.log). `native_agent_service_contract` verifies two concurrent actual transport streams, bounded admission and joined cancellation. `native_agent_http_contract` verifies real workspace/tool continuation, CLI submission, queued/running cancellation, admission rejection without orphan prompts, encrypted credential reuse, provider HTTP failure and forced-process restart recovery. It also uses the actual VS Code host client against the native server. None of these synthetic inference peers certifies a live model or completed coding task.

Live provider/coding validation, mutation/process tools and approvals, reconciliation of external effects, pushed event subscriptions, pagination, credential management endpoints, team authorization/TLS, PostgreSQL and actual VS Code UI validation remain required. Remote views and WebRTC are not implemented by this loopback service.

## Native effect authorization components

The schema-v3 operation journal is implemented behind the native repository/persistence service. A runtime proposal records its exact run, verified workspace identity, tool, argument bytes and expiry. A decision requires a controller identity derived from backend authentication, retained with its events and later outcomes. Controller decisions are single-use; executor claims must match all recorded fields and the exact JSON payload. Duplicate JSON fields and excessive nesting are rejected. Grants expire before use, and cancellation retires unused grants. Operation changes and their events commit atomically through xlang3. The loopback adapter supplies the authenticated `local-owner` identity; team authorization remains incomplete.

The states distinguish waiting for approval, granted, denied, expired, cancelled, executing, succeeded, failed and uncertain. Normal run completion is rejected with unresolved operations. Startup recovery marks interrupted execution claims uncertain and never regrants them. A workspace admits one executing effect at a time, and an uncertain effect blocks further effect claims in that same verified workspace identity, including new run/operation IDs. Backend callers must enforce controller authorization, verified workspace identities and overlapping-root policy before using this repository contract. Local inspection/decision routes are enabled; model write/process tools and uncertainty-reconciliation controls remain pending.

`PermissionWaiter` uses the same durable repository records to await a decision and return a single claimed operation. It supports stop-token cancellation, server-clock expiry, waitable workspace contention and explicit rejection of uncertain workspace effects. It never executes a tool; the owning runtime must journal the actual result after the claim. It polls durable records with a cancellable wait, so no transport/view callback authorizes an effect by itself. Repository and waiter contracts passed with real persistence and worker waits. They establish these component invariants, not product approval UI or live effect execution.

Public user-message validation requires `{role:"user",data:{content:"nonempty text"}}`, rejecting NUL, unsupported fields and non-text content before storage. The native model loop currently accepts text only; storing other payloads would poison later context construction. Rejection/no-history-change tests passed. Multimodal parts remain required work, not silently accepted data.

Current component evidence: [native-permission-planning-ctest.log](evidence/native-permission-planning-ctest.log), sixteen tests passed. The new contracts cover exact argument binding (including large numeric bytes), duplicate/deep JSON rejection, controller attribution, competing decisions/claims, cancellation/expiry, storage/event fault rollback, atomic schema-v2 migration, uncertain restart recovery, workspace exclusion and cancellable permission waits. The fixture-side file write in the repository contract is explicitly test code, not a production effect adapter.

The backend-only [approved edit executor](native-workspace-tools.md) now uses the real file adapter after a durable exact approval claim. Its separate contract verifies actual file changes and an outcome-storage fault followed by uncertain restart recovery. Executor-component evidence: [native-approved-edit-ctest.log](evidence/native-approved-edit-ctest.log), seventeen native contracts. The executor is not yet integrated into model execution; model write-tool integration and reconciliation remain required.

Current approval API evidence: [native-approval-api-ctest.log](evidence/native-approval-api-ctest.log), eighteen contracts. The compiled HTTP server, CLI and actual VS Code host client inspect and decide real native edit proposals. Real files establish approved changes, denial without effects and stale-content rejection. Tests also verify wrong-token rejection, actor/payload spoofing rejection, duplicate decisions/JSON fields and absent proposal/claim/outcome fabrication routes. The test driver owns run/proposal creation; no model, approval UI or live coding completion is claimed.
