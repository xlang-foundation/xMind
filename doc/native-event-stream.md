# Committed native event feeds

## Current production browser-controller acceptance

At xMind source `4701255b1489894c9b281cd74c4b551abe1ba38d`, the complete
`native_local_view_contract` passed in 188.95 seconds against the compiled
native executable and real loopback HTTP/browser adapter. The test ran the
production `BrowserController`, `BrowserSessionClient`, event reader and
`EventStreamSubscription`; it did not substitute native event replies. A real
paused agent graph was resumed with human input, and newly committed events
arrived incrementally before terminal history reconciliation. The test also
restarted the browser adapter at its origin and resumed from the accepted
cookie/cursor, detached and reattached without cancelling the graph, saturated
the native stream lease while preserving command access, and killed the view
owner to verify reauthentication without advancing or replaying the graph.
The same native-view test now registers an actual existing workspace folder
through the browser route and verifies profile selection and credential
rotation.

This is production browser-controller and native HTTP acceptance, not a
rendered-browser screenshot or installed VS Code acceptance. It used no
provider requests (`providerRequests: 0`), so it does not establish live model
inference. In the same run, the production VS Code extension controller and
`WorkspaceBackend` also drove a real native graph through pause, view retirement,
human input, file read and approved Unicode file creation; the resulting disk
bytes matched the native success receipt. VS Code workspace/webview APIs were
fixtures, so this does not establish a rendered or installed IDE.
[Original CTest output](evidence/native-local-view-browser-sse-local.log),
[source and binary hashes](evidence/native-local-view-browser-sse-local.json).

This passing result supersedes the earlier statements below that the prepared
production browser-controller, incremental-event, capacity and revocation
assertions had not executed. The broader installed/rendered and live-provider
requirements remain open.

The current gate also exercised a production-host approved creation case. A
registered human/tool graph proposed a Unicode file; the retired controller's
approval was rejected and the file remained absent. Approval through the
current controller created exactly one tool child, completed the graph and
returned a persisted success receipt whose hash/size matched independently
read disk bytes. VS Code comparison display is an API fixture; its documents
came from the actual native proposal. Installed writing, live coding inference
and full parity remain separate requirements.

Production extension-host acceptance ran in the same gate: actual `extension.js`,
shared reader/subscription and `WorkspaceBackend` ran against the compiled native
executable. Only VS Code workspace/webview APIs were fixtures; backend replies
were actual HTTP results. It verified paused-graph replay, sidebar retirement
and reopen without cancellation, rejection of input from the retired view,
checkpoint-bound input through the current view, a real file-read child and
exact terminal persisted history, with no periodic run timer. Installed/rendered
IDE and live-provider acceptance remain separate.

Clean frame-boundary EOF is now classified as transport unavailability after
preserving the last consumer-accepted cursor. The standalone reader still
rejects the absent native acknowledgement; the shared subscription can retry
only its read-only GET at that cursor, at most three times. Incomplete frames,
malformed JSON/UTF-8, ownership/authentication and consumer rejection do not
receive this classification. Two additional fixtures use the production reader
and subscription together to prove acknowledged replay and bounded exhaustion.
Complete **243 extension / 50 browser** synthetic suites pass.
[EOF recovery scope and original evidence](evidence/native-event-stream-eof-candidate.json).

The native browser acceptance restarted the actual browser adapter at its
existing origin while a native human/tool graph was paused. The prior cookie
and accepted cursor restored the same root/session, with no dependent read
before human input. This verifies access-adapter restart, not backend-process
restart or installed/rendered acceptance.

Native transport source `7a148ff` passed the complete hosted **110-contract**
gate and package verification. Its executed native view assertions compare
completed actual tool-graph graph/tree/run streams to persisted records,
Last-Event-ID replay/cursor rejection and scoped browser-cookie forwarding.
Downloaded artifacts independently verify the exact contract set and packaged
bytes/sources. At source `4701255`, production reader/subscription/controller,
active-feed, capacity and revocation paths subsequently passed local native
acceptance. The installed `689404e` profile is preserved; actual
installed/rendered streaming remains pending.
[Accepted transport scope](evidence/native-event-stream-hosted-7a148ff-scope.json).

Browser transport correction: the original page session subclass overrode JSON
requests but inherited bearer-authenticated streaming. It therefore could not
open a production browser feed. The shared reader now obtains its response
through a transport method; the production `BrowserSessionClient` overrides
that method and JSON requests with same-origin HttpOnly cookie transport.
Browser SSE never acquires or exports a backend bearer token. Native/editor
clients retain their protected bearer transport and abort-before/after-secret
checks. The current native integration described above exercises this production
browser client through the real browser adapter. Complete **241 extension / 49
browser** synthetic suites pass; rendered-browser acceptance remains pending.
[Correction scope and original evidence](evidence/native-event-stream-browser-cookie-candidate.json).

The full webpage fixture now runs all emitted classic scripts together and
streams through the actual cookie client, bridge and renderer. Synthetic
committed text/usage render in the live response; the terminal persisted history
replaces it once. The idle page issues no periodic run requests and disconnect
sends no cancellation or admission. Complete **241 extension / 50 browser**
suites pass. Initial fixture assertions were corrected to inspect the live
container and the native normalized usage fields. These DOM metrics remain
JSDOM/synthetic evidence; actual native event delivery is separately proven
above, and rendered-browser acceptance remains open.
[Full-page fixture scope and original logs](evidence/native-event-stream-browser-page-candidate.json).

Transport compilation and scoped replay integration passed at `7a148ff`.
Production browser and VS Code controller/active-feed integration subsequently
passed at `4701255`; the accepted `689404e` installation remains unchanged.
Rendered/installed acceptance is still separate.

The shared `BackendClient.eventStream` reader passes synthetic
parser/ownership/abort contracts and the production native-reader integration
recorded above. Its consumer-accepted cursor is the only resume point after
disconnection.
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
without a cancel command. The real native view contract also compares
single/graph/tree replay with actual saved records, rejects invalid cursors,
and exercises browser-cookie streaming, production reader/subscription,
incremental graph events and reconnect at source `4701255`.
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
native view contract exercises this production reader and scoped resume at
source `4701255`.

`EventStreamSubscription` supplies the shared observation lifecycle. It pins the
client/origin, root, conversation, scope and view generation; a matching watch
does not reopen the feed. A normal native connection rotation resumes its own
delivered cursor. Replacement/disposal aborts the old observation and rejects
late events, outcomes and retry callbacks. Only marked fetch/body transport
unavailability and HTTP 503 receive up to three bounded retries. Authentication,
protocol and consumer failures stop explicitly; a consumer `TypeError` cannot
be mistaken for a network failure. The complete **238 extension / 43 browser**
fixture suites pass. Actual native subscription and controller adoption passed
in the current native view contract.
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
pass with all 2421 tracked inputs unchanged. The native integration described
above additionally verifies actual event delivery; these fixtures do not prove
installed/rendered streaming acceptance.
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
fixture around the actual shared subscription and production extension code.
The native extension-host integration above verifies backend behavior;
rendered editor and live model execution remain open.
[VS Code candidate and original evidence](evidence/native-event-stream-vscode-adoption-candidate.json).

The real native view contract exercises the production browser controller
around an actual human/tool graph: stream while paused, detach without
cancellation, reattach, submit checkpoint-bound human input, observe incremental
committed events and verify the real read result and terminal history without
duplicate delivery. Instrumentation records actual calls/events; no native
replies are substituted. See the exact passing gate at the top of this file.

Further prepared assertions open two actual paused feeds using scoped browser
access directly against native: the third stream must receive HTTP 503 while
session commands remain available. Aborting one reader must leave the graph
paused. Killing its actual view owner must end the other stream with
`reauthenticate`, preserve the paused graph and leave its dependent read pending.
Both observers have bounded transport timeouts and are aborted during cleanup.
The capacity/revocation checks passed in the current native view contract.

Remaining acceptance is the full source-wide native gate, rendered/installed
browser and VS Code validation, live-provider streaming and backend-process
restart without replay. A2A streaming remains a separate protocol contract.
