# Committed native event feeds

Candidate transport implementation. **New native compilation/integration is
pending.** The accepted `689404e` installation remains unchanged. The existing
browser and VS Code controllers still use polling; actual reader integration,
reconnection and controller adoption are the next integration steps.

The shared `BackendClient.eventStream` reader is now implemented and passes
synthetic parser/ownership/abort contracts. Actual native-reader integration,
reconnection management and controller adoption remain pending.
[Reader candidate and exact verification](evidence/native-event-stream-client-candidate.json).

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

The production reader uses authenticated read-only GET, fatal incremental UTF-8
decode, exact root/session/scope metadata and canonical increasing committed
IDs. New child events require matching native child metadata before delivery.
The cursor advances only after the consumer accepts the event. Abort, invalid
frames, EOF and consumer rejection detach the feed and retain the last delivered
cursor; they do not send commands or start a fallback connection. A native end
frame must acknowledge that exact cursor. Connection/frame limits and transport
timeouts bound parsing. The complete **232 extension / 43 browser** fixture
suites pass with all 2421 tracked native/tool/view inputs unchanged. The real
native view contract now also exercises this production reader and scoped
resume, but those new assertions remain unexecuted.

`EventStreamSubscription` supplies the shared observation lifecycle. It pins the
client/origin, root, conversation, scope and view generation; a matching watch
does not reopen the feed. A normal native connection rotation resumes its own
delivered cursor. Replacement/disposal aborts the old observation and rejects
late events, outcomes and retry callbacks. Only marked fetch/body transport
unavailability and HTTP 503 receive up to three bounded retries. Authentication,
protocol and consumer failures stop explicitly; a consumer `TypeError` cannot
be mistaken for a network failure. The complete **238 extension / 43 browser**
fixture suites pass. Actual native subscription is prepared in the view
contract but has not run; UI controller adoption is still pending.
[Subscription candidate and original evidence](evidence/native-event-stream-subscription-candidate.json).

Remaining acceptance includes the complete native gate, actual incremental
delivery while a run continues, authorization revocation/expiry, saturated
streams, disconnect/reconnect and backend restart without replay, shared UI
adoption and fresh installed/rendered coding. Provider gateway streaming and
A2A streaming remain separate contracts.
