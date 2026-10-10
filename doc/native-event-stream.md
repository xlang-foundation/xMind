# Committed native event feeds

The production-host native acceptance also prepares an actual approved
creation case. A registered human/tool graph proposes a Unicode file; the
current sidebar opens the production read-only comparison, while the file
must remain absent. Approval from the retired sidebar must leave the operation
waiting and the file absent. Current-sidebar approval must create exactly one
tool child, complete the graph and produce a persisted success receipt whose
hash/size match independently read disk bytes. VS Code comparison display is
an API fixture; its documents come from the actual native proposal. These
assertions have only passed syntax/whitespace checks and remain unexecuted.
They do not prove installed writing, live coding inference or full parity.

Production extension-host acceptance is now prepared inside the native view
contract. It runs the actual `extension.js`, shared reader/subscription and
`WorkspaceBackend` against the compiled native executable. Only the VS Code
workspace/webview API is a fixture; backend replies are actual HTTP results.
Assertions require a paused graph's committed replay, sidebar close/reopen
without cancellation, rejection of input from the retired view, explicit
checkpoint-bound input from the current view, one real file-read child and
exact terminal persisted history. A periodic run timer is rejected by the
fixture. Syntax/whitespace checks pass; these assertions have not executed.
This will not establish installed/rendered IDE or live-provider acceptance.

Clean frame-boundary EOF is now classified as transport unavailability after
preserving the last consumer-accepted cursor. The standalone reader still
rejects the absent native acknowledgement; the shared subscription can retry
only its read-only GET at that cursor, at most three times. Incomplete frames,
malformed JSON/UTF-8, ownership/authentication and consumer rejection do not
receive this classification. Two additional fixtures use the production reader
and subscription together to prove acknowledged replay and bounded exhaustion.
Complete **243 extension / 50 browser** synthetic suites pass.
[EOF recovery scope and original evidence](evidence/native-event-stream-eof-candidate.json).

The prepared native browser acceptance now restarts the actual browser adapter
at its existing origin while a native human/tool graph is paused. It requires
a newly validated observation using the prior cookie and accepted cursor,
unchanged selected root/session and no dependent read before human input.
This gateway-restart assertion has not run. It does not establish backend
restart or installed/rendered acceptance.

Native transport source `7a148ff` passed the complete hosted **110-contract**
gate and package verification. Its executed native view assertions compare
completed actual tool-graph graph/tree/run streams to persisted records,
Last-Event-ID replay/cursor rejection and scoped browser-cookie forwarding.
Downloaded artifacts independently verify the exact contract set and packaged
bytes/sources. Later production reader/subscription/controller, active-feed and
capacity/revocation assertions have not run natively. The installed `689404e`
profile is preserved; actual installed/rendered streaming remains pending.
[Accepted transport scope](evidence/native-event-stream-hosted-7a148ff-scope.json).

Browser transport correction: the original page session subclass overrode JSON
requests but inherited bearer-authenticated streaming. It therefore could not
open a production browser feed. The shared reader now obtains its response
through a transport method; the production `BrowserSessionClient` overrides
that method and JSON requests with same-origin HttpOnly cookie transport.
Browser SSE never acquires or exports a backend bearer token. Native/editor
clients retain their protected bearer transport and abort-before/after-secret
checks. The actual native view contract uses this production browser client
through the real browser adapter. Complete **241 extension / 49 browser**
synthetic suites pass; actual native and rendered acceptance remain pending.
[Correction scope and original evidence](evidence/native-event-stream-browser-cookie-candidate.json).

The full webpage fixture now runs all emitted classic scripts together and
streams through the actual cookie client, bridge and renderer. Synthetic
committed text/usage render in the live response; the terminal persisted history
replaces it once. The idle page issues no periodic run requests and disconnect
sends no cancellation or admission. Complete **241 extension / 50 browser**
suites pass. Initial fixture assertions were corrected to inspect the live
container and the native normalized usage fields. This remains JSDOM/synthetic
transport evidence, not actual native or rendered-browser acceptance.
[Full-page fixture scope and original logs](evidence/native-event-stream-browser-page-candidate.json).

Transport compilation and scoped replay integration passed at `7a148ff`.
**New controller and active-feed integration is pending.** The accepted
`689404e` installation remains unchanged. The existing
browser and VS Code adoption described below are tested candidates; actual
native and installed/rendered acceptance remain.

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
contract but has not run; the controller candidates are described below.
[Subscription candidate and original evidence](evidence/native-event-stream-subscription-candidate.json).

The browser now uses that actual shared subscription for run, graph and owned
tree scopes. A committed callback updates the selected live view, while
observation/event/end callbacks request read-only metadata snapshots. Event
refreshes coalesce in a single 100 ms timeout; there is no periodic HTTP timer.
Snapshot requests serialize, so a newly observed child waits for an older
snapshot and then resolves its current owner before publication. Cursor checks
deduplicate overlapping REST and SSE delivery. View retirement cancels queued
refreshes and detaches observation only. Four additional controller fixtures
cover idle request counts, stale callbacks, overlapping events and new graph
children during a held snapshot. Complete **238 extension / 47 browser** suites
pass with all 2421 tracked inputs unchanged. This is synthetic controller
evidence, not actual native or installed/rendered streaming acceptance.
[Browser adoption scope and original evidence](evidence/native-event-stream-browser-adoption-candidate.json).

The production VS Code host now uses the same subscription, pinned additionally
to its actual view and configured backend origin. Periodic run polling is
removed; read-only snapshots serialize and refresh on observation/events/end.
Direct committed delivery awaits the webview acknowledgement. Child histories
refresh even when the SSE callback has already advanced the displayed cursor.
Selecting a completed historical run observes the active same-session feed
for metadata updates without displaying that other run's live events. Selection
intent, workspace changes and disposal abort only observation and invalidate
queued callbacks. Complete **241 extension / 47 browser** synthetic suites pass
with unchanged tracked inputs. The new host checks use a labelled transport
fixture around the actual shared subscription and production extension code;
they do not prove native, rendered editor or live model execution.
[VS Code candidate and original evidence](evidence/native-event-stream-vscode-adoption-candidate.json).

The real native view contract now also prepares production-browser-controller
acceptance around an actual human/tool graph: stream while paused, detach
without cancellation, reattach, submit checkpoint-bound human input, observe
incremental committed events and verify the real read result and terminal
history without duplicate rendering. The transport is instrumented only to
record its actual calls/events; no native replies are substituted. Syntax and
whitespace checks pass. These new assertions have not executed and do not
establish native streaming or rendered UI acceptance.

Further prepared assertions open two actual paused feeds using scoped browser
access directly against native: the third stream must receive HTTP 503 while
session commands remain available. Aborting one reader must leave the graph
paused. Killing its actual view owner must end the other stream with
`reauthenticate`, preserve the paused graph and leave its dependent read pending.
Both observers have bounded transport timeouts and are aborted during cleanup.
These capacity/revocation checks are also unexecuted.

Remaining acceptance includes the complete native gate, actual incremental
delivery while a run continues, authorization revocation/expiry, saturated
streams, disconnect/reconnect and backend restart without replay, actual shared
UI integration and fresh installed/rendered coding. Provider gateway streaming and
A2A streaming remain separate contracts.
