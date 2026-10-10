# Committed native event feeds

Candidate transport implementation. **New native compilation/integration is
pending.** The accepted `689404e` installation remains unchanged. The existing
browser and VS Code controllers still use polling; shared client parsing,
reconnection and controller adoption are the next integration steps.

| Read-only endpoint | Persisted scope |
| --- | --- |
| `GET /v1/runs/{id}/events/stream` | One run's committed events |
| `GET /v1/runs/{id}/tree-events/stream` | Root and its owned immediate children in the same conversation |
| `GET /v1/graph-runs/{id}/events/stream` | Validated graph root and its owned immediate children |

All endpoints retain native Host, owner/view authentication and browser-origin
boundaries. Graph/tree observation requires a root. No stream route admits,
resumes, approves or cancels execution. Stream loss detaches observation.

`after` and `Last-Event-ID` use a nonnegative public safe integer. Duplicate,
malformed, unknown-query, conflicting and foreign/future cursors are rejected.
A nonzero cursor must identify an actual event inside this exact feed. Events
use the existing public event DTO; their IDs are the actual committed SQLite
sequence, read through xlang3. No process-memory inference result is substituted.

The initial `observation` frame contains the actual run, cursor and scope.
Each `committed` frame has `id: SEQUENCE` and the existing
`{seq, run_id, kind, data}` payload. An `end` frame supplies the last successfully
written cursor and `terminal`, `reconnect`, `reauthenticate` or `interrupted`.
Comments are heartbeats. Connection loss without an end frame is interrupted
observation; it is not evidence of run failure or permission to replay effects.

State is read before each event batch so final committed events drain before a
terminal end. Batches contain at most 16 events; committed frames are capped at 16 MiB and
committed bytes at 32 MiB per connection. Connections rotate after 25 seconds.
The shared two-stream lease ceiling includes existing A2A streams, preserving
non-streaming worker capacity in the four-worker HTTP pool. Revoked, expired or
dead-process view access is rechecked while streaming. These limits are resource
bounds, not a throughput or performance claim.

The native view uses the pinned cpp-httplib `open_stream`/`StreamHandle::read`
API to forward incremental bytes after inspecting the actual upstream status
and content type. It does not buffer a whole stream or export the backend key.
The browser adapter retains cookie/origin enforcement, forwards the cursor,
honors response backpressure and closes only its upstream observation on detach.
Both adapters bound total forwarded bytes; neither owns execution or SQLite.

The complete local **224 extension / 43 browser** fixture suites pass with all
2421 tracked native/tool/view inputs unchanged during the tests. A synthetic
HTTP peer separately proves browser delivery before peer completion and detach
without a cancel command. That is adapter evidence, not native streaming.
The real native view contract now compares single/graph/tree replay with actual
saved records, checks invalid cursors and exercises browser-cookie streaming.
Those new assertions have not executed yet.
[Candidate and exact verification scope](evidence/native-event-stream-transport-candidate.json).

Remaining acceptance includes the complete native gate, actual incremental
delivery while a run continues, authorization revocation/expiry, saturated
streams, disconnect/reconnect and backend restart without replay, shared UI
adoption and fresh installed/rendered coding. Provider gateway streaming and
A2A streaming remain separate contracts.
