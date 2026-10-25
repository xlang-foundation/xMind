# Captured connection binding design

Status: captured Local SDK objects, the shared native HTTP adapter and browser/
extension prompt admission are implemented. The development candidate also
implements authenticated negotiation, cached scope discovery and explicit
scoped remote session/run methods, durable command receipts, run-only event
consumers and snapshot recovery objects. These are separate native service
objects, not profile-wide Remote activation. Unified view command routing,
workspace/effect enrollment, root-tree recovery and worker fencing remain open.
The implemented catalog API is documented in connection-profiles.md. This design
extends that public contract; it does not include private Nexus implementation.
Installation and source qualification remain separate checkpoints. Current
evidence and its limitations are recorded in VALIDATION_STATUS.md.

The [generic remote v1 proposal](generic-remote-protocol-v1-proposal.md) makes
negotiation, authentication, receipts, enrollment and lease boundaries concrete
for architecture review. It is not a deployed endpoint or Nexus compatibility
claim; remote execution capability remains unavailable. The scoped metadata
read is verified with controlled peers and has separate TLS acceptance limits.

## Public native objects

Keep the registered xmind root and its authenticated Connections(owner_token)
subobject. Open(profile_id, expected_profile_revision) returns a captured
binding object. Root remains a registration surface, not a command dispatcher.
The Local binding exposes Identity() and scoped Sessions, Runs, Graphs, Models,
Commands and Workspace service objects, plus Events(session_id, root_run_id)
and RunEvents(session_id, run_id).
Use ordinary SDK method calls and SDK list/dictionary values for local IPC.
Do not add peer.call routes, custom remote-value envelopes or a second serializer.

`Connections.OpenScope(profile_id, expected_profile_revision, scope_id)` creates
a distinct captured remote object with Identity, Sessions and Runs services,
plus `RunEvents(session_id, run_id)`. Sessions expose metadata and typed creation;
Runs expose metadata, typed submission/cancellation and receipt reconciliation.
These explicit methods still require their individual negotiated permissions.
They do not enroll a workspace or authorize a local worker/tool effect.

`RunEvents(...).Consumer(consumer_id)` exposes durable Open/Read/Accept methods.
Its `Snapshot(expected_checkpoint_revision)` and `ResumeSnapshot(snapshot_id)`
return recovery objects with Open/Read/Observe/Reset methods. Reset acknowledges
an explicitly observed complete snapshot with matching revision and digest;
observation alone does not advance the consumer checkpoint or replay work.
The native connector validates the encrypted grant and typed HTTPS protocol;
the SDK owns IPC value transfer and method dispatch. This does not change the
Local Open API or route the active Local view implicitly to a Remote profile.
See the exact wire schema
and limitations in the [generic remote contract](generic-remote-protocol-v1-proposal.md).

Implementation and acceptance are distinct: installed76 has its recorded
controlled-peer acceptance; the newer SDK35fa source passes its actual local184
inventory and rendered cached-snapshot checks. Native Windows positive trusted
TLS for that newer source remains pending hosted qualification. See
[current validation evidence](VALIDATION_STATUS.md).

Identity contains the stable connection/profile ID, profile revision, verified
server identity, negotiated protocol version and captured backend generation.
The public identity contains no credential or reusable authentication reference.
Local server identity is persisted once through the existing SQLite/xlang3
information contract; it is not a filesystem path, machine name or owner PID.
The owner generation still fences live objects when that backend retires.

Open must reject unavailable or unauthenticated remote profiles. A label, URL,
capability flag or client-supplied team ID cannot create an authenticated binding.
The native connector owns transport authentication and returns a verified grant;
only then can the connection service bind its identity and authorized scope.
Workspace enrollment is a separate service and authorization step.

## Ownership rules

A view holds its own captured binding. Selecting another connection changes only
future view commands. Existing admitted work retains the binding under which it
was admitted; it is never transferred, cancelled or replayed by a selector change.
The catalog active_id remains compatibility/default-selection metadata, not the
authority used to reinterpret an existing object, session, cursor or run.

A metadata/profile revision change rejects new admissions through a stale binding.
Read/status/reconciliation operations for previously admitted work retain their
original server/connection scope. Revocation/expiry closes new effects and invokes
explicit in-flight reconciliation; it does not turn a remote failure into Local
execution. Backend retirement rejects every captured native service object before
its persistence/executor resources are destroyed, as current root lifetime does.

Existing root Sessions and Runs methods remain explicitly Local compatibility
services. They must not begin following global selection implicitly. New clients
use the binding service objects and carry their captured context throughout.

A session/run identifier is meaningful only within its captured connection and
server identity. Event cursors bind server, connection, session, root run and sequence.
Resuming a cursor against another binding must fail before events are read.
Remote shared records remain server-owned; Local SQLite stores Local records,
connection/authentication metadata and local effect receipts. No direct remote
SQL access and no copying remote session records into a Local session namespace.

## Native service boundary

Implement a C++ binding interface with typed session/run/approval/model/event
operations. Local implementation calls existing native services. Authenticated
remote implementation uses the negotiated command/event contract over verified
encrypted transport. UI and CLI adapters do not implement execution, persistence,
permissions, authentication or fallback decisions.

Credentials remain encrypted on the owning backend, separately from public
profile metadata. Grant references are bound to server identity, profile revision,
account/authorized scope and expiry. Profile edits cannot preserve an old grant
for a different origin. Request authorization and negotiated capability support
are independent checks; capability discovery grants no authority.

Retain existing domain request/run IDs and durable admission/effect receipts.
Transport correlation IDs are not effect idempotency. Lost replies require query
and reconciliation against the authoritative connection before retry. The SDK's
internal call correlation and value serialization need no application replacement.

## Delivery sequence and proof

The Local service and HTTP adapter share native binding lifetime, revision and
admission checks. HTTP run requests support model/provider, workspace revision,
file attachments and queued inbox options. The scoped SDK Runs.Start accepts id, session and prompt; Runs.Submit accepts
those additional options through an ordinary SDK dictionary using the same
native request validation and admission dispatch. SDK value serialization stays
inside xlang3.

Browser and extension clients negotiate Local admission support, capture a
binding for session creation and prompt submission, and validate returned
identity and ownership. CLI scoped session/run/event commands use SDK subobjects.
Operation inspection and decisions use native captured run-scoped services via
SDK and HTTP. Browser/extension clients pass the observed session/run and validate
binding receipts; run inspection and cancellation use the same captured handle.
Captured model catalog/selection, graph controls/tree streams, commands and
workspace metadata/membership/file reads now have native SDK/HTTP and shared-client
integration. Provider configuration mutation and other general operations still
require integration.
Actual browser and CLI coding receipts exist; actual rendered VS Code
coding remains unverified. These results do not establish remote execution.

The current production source2d98bdde passes the complete173-contract native
inventory with frozen source/runtime/dependency closures. Accepted client481/165
assets and the exact qualified0.1.66 runtime pass separate actual browser coding,
CLI coding and managed package handoff checks. The installed66 isolated smoke
also passes; protected-user-state verification passes with all five roots unchanged. These receipts
do not prove authenticated remote routing or rendered VS Code coding. See
[evidence and scope](evidence/candidate-0.1.66-qualified-coding-20261024.json)
and [installed smoke](evidence/installed-0.1.66-smoke-20261024.json).

1. Add captured Local binding and scoped service objects to the registered root.
   Exercise actual independent SDK processes, HTTP/SDK agreement, wrong token,
   stale profile admission, retained results and retired-generation refusal.
2. Carry binding context through CLI/browser/extension session, run, approval,
   model and event adapters. Verify two simultaneous views cannot cross session
   identities/cursors. Keep existing Local compatibility routes working.
3. Specify public protocol negotiation and authenticated connector integration;
   implement the generic native client without private team/server internals.
   Test verified transport, server mismatch, incompatible version, expired grant,
   revoked scope, missing capability and unavailable server with controlled peers.
4. Add explicit workspace/effect enrollment, expiring fenced leases and durable
   receipts. Verify two workers, duplicate assignment, revocation, reassignment,
   lost replies and restart reconciliation before claiming distributed execution.
5. Complete source-wide and client/rendered verification, then package/install
   with protected state and owner preservation. Focused checks do not replace
   the complete native run or actual rendered VS Code/editor-context evidence.

Nested process planning/recovery remains another required delivery area. This
binding design does not remove that requirement or count private Nexus server,
PostgreSQL/team scheduling, WebRTC, Electron or broad runtime optimization as OSS
work. Do not claim the goal complete until every acceptance area in
../docs/xmind-delivery-acceptance.md has authoritative evidence.

## Single-run event observation

`binding.RunEvents(session_id, run_id)` returns a standard registered xlang3 Events subobject. Call `Cursor()` for its initial cursor and `Read(cursor)` for committed records. SDK values use the existing native list/dictionary conversion. The backend validates the session/run and cursor namespace on every read; child runs are supported and no descendant records enter a run-only page. `Events(session_id, root_id)` retains root-tree semantics. Both object types reject reads after backend retirement.

Independent clients can use `xmind ipc --endpoint lrpc:PORT connections run-events local PROFILE_REV SESSION RUN JSON_CURSOR`, with `null` as the initial cursor. The HTTP adapter exposes `GET /v1/connection-bindings/{binding}/sessions/{session}/runs/{run}/events/stream?after=SEQUENCE`, negotiated by `local_connection_run_stream:1`. Opening observations carry the captured identity; clients must retain that binding on reconnect and refuse a changed identity. This is read-only observation, never execution replay. [Focused validation](evidence/captured-run-stream-clients-20261023.json).

## Captured model catalog

`binding.Models().List()` reads the captured backend model catalog through ordinary xlang3 SDK calls. It returns binding identity, default_model and models (id entries); no provider credentials are exposed. Independent clients use `xmind ipc --endpoint lrpc:PORT connections models local PROFILE_REV`. The HTTP adapter is `GET /v1/connection-bindings/{binding}/models`, negotiated by `local_connection_models:1`. Existing Local `/v1/models` and the captured adapter share the native encoder. Reads retain the original generation and reject backend retirement. Session model selection/provider changes remain separate APIs. [Validation](evidence/captured-model-catalog-clients-20261023.json).

## Captured session model choice

`binding.Models().Open(session)` returns `{binding, model}` with the existing session model state. `Select(session, request)` uses exactly `{model_id, provider_profile_id, expected_provider_revision, expected_revision}`. A null model_id clears the override. Session and active provider revisions must match; the native provider runtime retains idle-state and route/catalog admission rules. The connection profile revision must also remain admitted before a mutation. Read-only observation retains its original backend generation.

The HTTP adapter is GET/POST `/v1/connection-bindings/{binding}/sessions/{session}/model`, negotiated by `local_connection_model_selection:1`. CLI SDK calls are `connections model-open ID REV SESSION` and `connections model-select ID REV SESSION JSON_OBJECT` after `xmind ipc --endpoint lrpc:PORT`. The SDK uses registered Models methods and native list/dictionary conversion. [Evidence and limits](evidence/captured-model-selection-clients-20261023.json) distinguish actual native HTTP selection from positive separate-process SDK/rendered selection still needing acceptance.

Positive separate-process `Models.Open/Select` is now verified: distinct xmind.exe clients select/clear an actual native provider-bound model, stale revisions reject, and backend restart preserves revision2 with no inference. [Focused SDK evidence](evidence/captured-model-selection-sdk-positive-20261023.json). Rendered selection remains separate.

## Captured workspace commands

`binding.Commands().List()` returns binding identity, workspace_id/authority_id and the native Markdown command catalog. `Invoke(session, request)` accepts id, command_id, optional arguments/model/provider binding/graph binding, and required expected_workspace_id/expected_workspace_authority_id. SDK callers supply a unique run id; the HTTP adapter can generate it. Templates expand as prompt text in the backend and never execute as shell/scripts. Single and graph submissions retain the existing native admission path.

CLI methods are `connections commands ID REV` and `connections command-invoke ID REV SESSION JSON_OBJECT` after `xmind ipc --endpoint lrpc:PORT`. HTTP uses GET `/v1/connection-bindings/{binding}/commands` and POST `/v1/connection-bindings/{binding}/sessions/{session}/commands`, negotiated by `local_connection_commands:1`. Clients retain immutable connection identity; a refused admission must never fall back or replay through another profile. [Exact acceptance scope](evidence/captured-commands-clients-20261024.json) includes positive SDK/HTTP graph-backed reads; ordinary model-backed and rendered command acceptance remain separate.

Ordinary non-graph Commands.Invoke is now verified through a separate xmind.exe SDK client: one controlled model request, persisted user/assistant pair, literal arguments and duplicate-ID refusal without repeated inference. [Focused evidence](evidence/captured-ordinary-command-sdk-20261024.json). Rendered browser/editor command acceptance remains separate.

## Captured workspace metadata

`binding.Workspace().Info()` returns `{binding, workspace}` with configured/root/workspace_id/authority_id. `Membership()` returns `{binding, membership}` with the existing revision/folder snapshot. Reads retain their original backend generation, revalidate workspace identity and reject retirement. Info reports an unconfigured workspace when there is no executor; Membership requires an opened native root.

CLI methods are `connections workspace-info ID REV` and `connections workspace-membership ID REV` after `xmind ipc --endpoint lrpc:PORT`. HTTP GET routes are `/v1/connection-bindings/{binding}/workspace` and `/workspace/membership`, negotiated by `local_connection_workspace:1`. Existing Local compatibility routes share their encoders. [SDK, gateway and shared-client evidence](evidence/captured-workspace-clients-20261024.json) does not establish file browsing or remote routing.

## Captured workspace file reads

`Workspace.Files(folder_id, membership_revision, path)` lists files; `File(...)` previews a native text file; `Find(folder_id, membership_revision, request)` searches filenames. Find accepts query and optional path/type/limit/hidden/respect_ignore using existing native limits. Folder IDs are checked against backend-owned membership and native identities; no caller-selected root path is opened. A stale revision or changed membership refuses.

CLI methods are `connections workspace-files ID REV FOLDER MEMBERSHIP_REV PATH`, `workspace-file` with the same arguments, and `workspace-find ID REV FOLDER MEMBERSHIP_REV JSON_OBJECT`. HTTP read-only POST routes are `/v1/connection-bindings/{binding}/workspace/files`, `/file`, `/find`, with folder_id/expected_workspace_revision plus path or search fields. `local_connection_workspace_files:1` advertises the native protocol. Results include binding, workspace_id, folder_id, revision and path. [Focused SDK/HTTP evidence](evidence/captured-workspace-files-sdk-20261024.json); browser integration remains separate.

The shared client now routes explorer listing/preview/search through captured Files/File/Find after native capability negotiation. The browser renderer calls those methods instead of constructing read URLs. Membership/folder/revision and binding are validated; protocol/identity errors never retry a compatibility route. Actual managed-view/gateway file operations and complete client481/165 pass. [Integration evidence](evidence/captured-workspace-files-clients-20261024.json) retains actual rendered acceptance as separate work.

## Captured provider profiles

`binding.Providers().List()` returns `{binding, providers}` with revision, active profile, public profile metadata and backend route policy. `Select(id, expected_provider_revision)` selects through the owning native runtime; connection profile revision and backend generation remain captured independently. Local SDK callers use `connections providers ID REV` and `connections provider-select ID REV PROVIDER_ID PROVIDER_REV` after `xmind ipc --endpoint lrpc:PORT`. All value transfer uses the supported xlang3 SDK.

HTTP reads use GET `/v1/connection-bindings/{binding}/providers`; selection uses POST `/v1/connection-bindings/{binding}/providers/select` with exactly id and expected_revision. Health advertises `local_connection_providers:1`. Shared clients retain immutable identity and never fall back after negotiated refusal. Setup Save/Discover/Import now also have captured SDK/HTTP adapters, documented below. [Evidence and limits](evidence/captured-provider-profiles-20261024.json).

## Captured provider setup

The Providers subobject accepts ordinary SDK dictionaries through Save(request), Discover(request) and Import(request). Save requires id, route_id, model and expected_revision; optional api_key enrolls/replaces a credential and optional activate is Boolean. Omitting api_key reuses the existing backend-owned encrypted credential. Discover requires id, route_id and expected_revision, with optional api_key. Caller-supplied destinations are refused. Import requires an absolute local path and expected_revision; the existing backend YAML parser and policy own validation and publication. No credential/reference enters public metadata.

HTTP uses POST `/v1/connection-bindings/{binding}/providers/save`, `/models` and `/import`, negotiated by `local_connection_provider_setup:1`. Save/import return `{binding, providers}`; discovery returns `{binding, models}`. Existing local setup routes remain compatibility adapters. Shared clients retain captured identity and never fall back after a negotiated refusal. The diagnostic CLI supports provider-save/provider-models/provider-import with one SDK dictionary argument; supply credentials through the authenticated SDK/HTTP request, never command-line arguments.

[Exact acceptance](evidence/captured-provider-setup-20261024.json) distinguishes positive separate-process save/import/restart from positive native HTTP discovery and the still-unverified rendered setup scope.

Positive separate-process Providers.Discover now has [direct SDK evidence](evidence/captured-provider-sdk-discovery-20261024.json): distinct xmind.exe callers share the actual captured identity, the backend uses its saved credential against a controlled catalogue, and exactly one additional catalogue request returns the expected deduplicated models without revision changes. This remains separate from rendered setup and live-provider acceptance.

## Captured session permission catalog

`binding.Permissions(session_id)` returns an ordinary registered SDK subobject. Pending() returns `{binding, session_id, requests}`; Grants() and Revoke(grant_id, expected_revision) return `{binding, session_id, revision, grants}`. The native grant store owns pending collection and scope metadata. Reads retain their original captured generation/profile identity; new revocation requires an admitted profile revision and uses the existing grant CAS. Backend retirement fences every object.

Health advertises `local_connection_permission_catalog:1`. HTTP GET uses `/v1/connection-bindings/{binding}/sessions/{session}/permissions` and `/permission-grants`; DELETE `/permission-grants/{grant}` accepts exactly expected_revision. The authenticated origin-bound view policy and browser gateway admit only the specific methods and paths. Existing compatibility DELETE is also fixed. The shared client checks returned binding/session, refuses downgrade and never retries through a compatibility route after negotiated refusal.

Diagnostic SDK CLI methods are connections permissions ID REV SESSION, permission-grants ID REV SESSION and permission-revoke ID REV SESSION GRANT GRANT_REV after `xmind ipc --endpoint lrpc:PORT`. Permission replies and reusable-grant admission are separate migration work. [Evidence and exact limitations](evidence/captured-permission-catalog-20261024.json).

## Atomic captured permission replies

`binding.Permissions(session_id).Reply(operation_id, request)` accepts decision allow/deny and optional Boolean grant_resource. Actor and scope come from the backend. Denial cannot create a reusable grant. The response is `{binding, session_id, operation}`. A reusable grant, ready operation and event commit atomically in native SQLite through xlang3; expiry or operation/event failure cannot leave authority behind. The grant-only backend helper has been removed.

HTTP POST `/v1/connection-bindings/{binding}/sessions/{session}/permissions/{operation}/reply` uses the same strict request parser and native method, negotiated by `local_connection_permission_reply:1`. The compatibility reply route also uses the atomic backend. Shared clients verify binding/session/request and never fall back after negotiated refusal. Reusable grants reject a stale connection profile; decisions on originally admitted work retain their captured scope.

Diagnostic CLI syntax is `connections permission-reply ID REV SESSION OPERATION JSON_OBJECT` after `xmind ipc --endpoint lrpc:PORT`. [Independent SDK/HTTP and rollback evidence](evidence/captured-permission-reply-20261024.json) also retains the unexplained earlier sidebar observation failure. It does not establish rendered editor acceptance or full native qualification of this source.

## Captured named agents

The named-agent adapter uses `binding.Agents()` as an ordinary registered xlang3 subobject. This follows Cantor's `WorkerMode::WorkerLoop` in `D:/CantorAI/Cantor/src/Tasks/WorkerProcess.cpp`: the SDK imports the registered root, obtains a service object and invokes its methods. It requires no application-owned call envelope or value serializer. Existing native `RunExecutor`/`ExecutionPlatform` agent methods and embedded-xlang3 persistence remain authoritative.

| Method | Native behavior and result |
| --- | --- |
| `List()` | Return `{binding, agents}` using the existing ordered public agent metadata. |
| `Definition(id)` | Return `{binding, agent}` using the persisted definition, including instructions, tool allowlist and ordered permission rules. This does not grant access or effects. |
| `Open(session_id)` | Return `{binding, session_id, revision, editable, selected}` using the existing session agent snapshot. |
| `Select(session_id, request)` | Accept exactly `agent_id`, `agent_revision` and `expected_revision`; resolve metadata in the backend and call the existing native session-selection method. Clear selection with null ID and definition revision zero. |

Reads retain their original captured backend generation and reject retirement. Selection also admits the captured connection-profile revision and backend owner, validates safe integer revisions, rejects changed/missing definitions, and keeps the native refusal for selecting a subagent as the primary agent. Clients never supply a definition snapshot as authority. Existing persistence owns idle/editability checks, the session revision and agent-switch events; the adapter must not duplicate those rules.

The authenticated Local routes are GET `/v1/connection-bindings/{binding}/agents`, GET `/agents/{id}`, and GET/POST `/sessions/{session}/agent`, advertised as `local_connection_agents:1`. SDK and HTTP share parsing/projection. After capability negotiation the shared browser/extension client validates returned binding/session identities and refuses downgrade or compatibility replay. Existing compatibility routes delegate to the same native validation and projection.

Focused native4 verifies imported definitions selected/cleared by distinct SDK callers, SQLite restart persistence, stale definition/session/profile refusal, retired objects and origin-bound HTTP. Complete clients488/171 pass. Actual browser selection and clear use captured routes; one controlled model request receives the selected native guidance, and reload retains its original history/selection. The initial view-auth401 and explicit-New failure on an unavailable legacy provider catalogue remain retained. Both UI adapters now respect explicit provider-admission capability during conversation selection. [Exact scopes and retained failures](evidence/captured-agents-20261024.json). This is not full native qualification or an installation. Authenticated remote enrollment, context/skills/inbox adapters, deeper scheduling/recovery and rendered VS Code remain separate requirements.

## Captured context and skills services (2026-10-24)

The binding returns ordinary registered SDK subobjects. Contexts.Open(session,
model) reads the native context snapshot; Request(session,id,model) observes one
manual request; Compact(session,request) accepts id and expected_head_revision,
optional model_id, and provider_profile_id/expected_provider_revision together.
The backend owns the actor and existing provider/context admission. An empty
model string selects the native default for SDK read calls.

Skills.List() reads the backend workspace catalogue. Open(session) reads persisted
selection and manual provenance. Select(session,request) accepts exactly ids,
expected_revision, expected_workspace_id and expected_workspace_authority_id.
Native workspace authority, idle ownership and optimistic revision rules apply.
The SDK transfers ordinary values; no custom serialization or call envelope is
introduced. Reads retain their captured generation; mutations require the
admitted connection revision; all objects refuse retirement.

HTTP uses GET /v1/connection-bindings/{binding}/skills and GET/POST
/sessions/{session}/skills. Context uses GET /sessions/{session}/context,
GET /context/requests/{id}, and POST /context/compact. Only context reads accept
the existing strict model_id query. Native health advertises
local_connection_context:1 and local_connection_skills:1. The shared client
validates binding/session receipts, refuses downgrade and never retries a
negotiated refusal on a compatibility route. Existing compatibility encoders and
admission helpers are shared with these adapters.

Diagnostic CLI methods after xmind ipc --endpoint lrpc:PORT connections are
skills ID REV; skills-open ID REV SESSION; skills-select ID REV SESSION JSON;
context-open ID REV SESSION MODEL; context-request ID REV SESSION REQUEST MODEL;
context-compact ID REV SESSION JSON. Each CLI invocation opens a fresh binding,
so it must supply the current profile revision. This differs from retaining a
previously opened object for read-only observation.

Focused native6/6 in24.81s and complete clients491/172 pass. Actual separate SDK
clients and origin-bound native HTTP verify context observations and persisted
skill selection/clear; the controlled lifecycle fixture verifies Compact
admission without actual provider work. Positive independent-provider compaction
and actual rendered controls remain separate, as do full native qualification
and installation. The retained failed fixture incorrectly reused revision1 for
a fresh CLI open after changing the profile to revision2. [Evidence and limits](evidence/captured-context-skills-20261024.json).

## Captured inbox and shared backend dispatch (2026-10-24)

binding.Inbox(session) is a standard registered xlang3 subobject. List() returns
binding, session_id and items. Enqueue(request) accepts id, content, optional
files and expected_workspace_id/expected_workspace_authority_id/
expected_workspace_revision. SDK callers supply a unique id; HTTP can generate
one. The backend validates attachment membership before persisting metadata.
The result contains binding, session_id and item. Exact duplicate accepted input
retains its original row/run; changed content/scope conflicts and cancelled IDs
cannot be reused. Cancel(id) acknowledges cancellation_requested and the same
binding/session/id. It removes queued input; it does not interrupt an already
claimed run. Original captured cancellation remains permitted after connection
metadata changes, as an authority-reducing operation. Native owner admission and
backend generation still apply.

HTTP GET/POST /v1/connection-bindings/{binding}/sessions/{session}/inbox and DELETE
/inbox/{id} use exact method/query/body rules and origin-bound authorization.
local_connection_inbox:1 negotiates client routing. The shared client validates
binding/session/input/claim metadata, never falls back after negotiated refusal,
and does not resend a mutation after a lost acknowledgement. CLI commands after
xmind ipc --endpoint lrpc:PORT connections are inbox ID REV SESSION,
inbox-enqueue ID REV SESSION JSON and inbox-cancel ID REV SESSION INPUT.

SessionInboxService now owns scheduling/recovery outside HTTP. The old HTTP
dispatcher is removed. IPC ownership is accepted before recovery starts; a
duplicate wrapper cannot start work. HTTP borrows the accepted backend service;
stopping that adapter leaves the externally owned IPC queue intact. Backend
retirement closes admission and joins its dispatcher before borrowed persistence
and executor objects are destroyed. Existing queue rows, provider/workspace
checks, input claims and SQLite ownership rules remain native.

Focused8 native and complete493/175 client contracts pass. The same-process SDK
lifecycle fixture uses real SQLite with controlled execution to verify recovery
and one claim without an HTTP listener. Separate CLI processes and actual native
HTTP verify queue/control scope. A hidden rendered browser selects a skill,
queues once behind a paused graph, restores both after reload and cancels once
with an acknowledgement; zero model requests. A retained observer-induced lost
reply reconciles via readback without replay. [Evidence and precise limits](evidence/captured-inbox-20261024.json).
