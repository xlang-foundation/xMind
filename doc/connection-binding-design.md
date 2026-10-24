# Captured connection binding design

Status: captured Local SDK objects, the shared native HTTP adapter and browser/
extension prompt admission are implemented. Authenticated remote functionality
and the remaining client operations are proposed.
The implemented catalog API is documented in connection-profiles.md. This design
extends that public contract; it does not include private Nexus implementation.
The installed 0.1.62 checkpoint remains separate from source validation. Current
evidence and its limitations are recorded in VALIDATION_STATUS.md.

## Public native objects

Keep the registered xmind root and its authenticated Connections(owner_token)
subobject. Open(profile_id, expected_profile_revision) returns a captured
binding object. Root remains a registration surface, not a command dispatcher.
The Local binding exposes Identity(), Sessions(), Runs() and
Events(session_id, root_run_id) and RunEvents(session_id, run_id) as scoped service objects.
Use ordinary SDK method calls and SDK list/dictionary values for local IPC.
Do not add peer.call routes, custom remote-value envelopes or a second serializer.

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
Model, graph, stream and other client operations still require integration.
Actual browser and CLI coding receipts exist; actual rendered VS Code
coding remains unverified. These results do not establish remote execution.

The latest SDK lifetime fix passed SDK contracts, repeated runtime isolation and
the required performance gate. The complete b133a0ee local xMind run passed
172/172; later SDK/HTTP operation adapters have focused acceptance and actual
browser/CLI proof. Earlier failed Rust and hosted runs remain recorded. Hosted
validation is tracked separately; no new package is qualified by focused results.

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
