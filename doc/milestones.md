# Reviewable milestones

Schema-v14 checkpoint `15c87c0` passes the complete local Release native gate:
**118/118 contracts** in **366.56 seconds**. It updates disposable historical
database fixtures to remove only empty agent selections, refreshes current-schema
assertions and lets the legacy owner handoff safely snapshot current schema-v14
repositories. The [complete CTest log and provenance](evidence/native-schema14-local-20261010.json)
are recorded. Client sources did not change; the existing rendered webpage
workspace acceptance remains **61/61**. OpenCode parity gaps and live model-backed
coding acceptance remain open.

The current Windows native build passes rendered standalone-webpage workspace
acceptance at source `ed5b84d`: the 648-pixel page registered a second isolated
workspace, switched between both folders, and restored the selected workspace
after refresh without re-entering the server token. No provider request or
workspace-file change occurred. The browser suite passes **61/61**. Earlier
current-browser evidence also verified CLI reattachment to an existing profile
owner; same-session/event inspection across clients remains a separate gate.
[Exact current rendered evidence](evidence/browser-workspace-live-ed5b84d.json).

At source `59f46791c02cb18cd4aa4c66b7d01b22f06cbcb0`, managed local-profile
startup verifies the complete source runtime once, then authenticates protected
profile state and uses the verified managed-copy path for server startup and
workspace reattachment. This removes redundant full-package scans while keeping
complete explicit revalidation available; a changed managed runtime is rejected
by the verifier contract. The rebuilt Windows binary passed the local-profile,
runtime-generation and native/browser workspace contracts. The standalone
webpage suite passed **60/60**. The currently open webpage still uses an older
backend and keeps Add workspace disabled; fresh rendered acceptance against the
rebuilt backend remains open. [Exact scope](evidence/native-profile-workspace-59f4679.json).

Native authoring now accepts bounded YAML graph catalogs plus JSON/YAML/Markdown
agent-instruction supplements. The new YAML converter rejects duplicate keys,
aliases, multiple documents and oversized values before existing native graph
validation; accepted records persist through embedded xlang3 SQLite. Graph and
instruction persistence tests passed in the complete **118/118 native CTest**
run. This is the authoring foundation only: named agents/tools, skills, `.py`
callables and the xlang3 programming API remain open. [Supported formats and
limits](authoring.md).

The current `native_local_view_contract` passed at source `4701255` in 188.95
seconds. The actual native backend, browser adapter and production browser
controller completed workspace registration, scoped incremental graph events,
cookie/cursor reconnect, detach/reattach, stream-capacity and view-owner
revocation checks. In the same run, production VS Code controller/backend code
completed a native pause/input/read/approved-create path and matched the actual
disk bytes to the native receipt. No provider calls were made. VS Code APIs are
fixtures; a rendered browser/IDE and installed-client acceptance remain open.
[CTest output and provenance](evidence/native-local-view-browser-sse-local.json).

The local managed-profile startup repair now passes the native profile contract
**1/1** in **76.79 seconds** and runtime-generation contract **1/1** in **6.07
seconds**. The copier now closes its native manifest writer before the read-only
inventory verifier reopens that file; transient Windows sharing violations get
a bounded retry. A live CLI `profile-info` also reattached to the same backend
PID and `D:\CantorAI2026\TestProj` workspace as the VS Code launch (`started`
false). This is local source/profile evidence, not a rebuilt release package or
full rendered VS Code acceptance.

The storage source `57da085` passes the complete hosted **118 native** gate in
**360.18 seconds**, **257 extension / 55 browser** checks and packaging. Independent
downloads verify exact inventory, both archive digests, all runtime/source-bundle
bytes, sixteen packaged-source comparisons and twelve accepted browser assets.
Actual durable xlang3 SQLite storage, trusted synthetic login/refresh primitives
and owned machine-root cleanup are verified. The subsequent manual renewal
coordinator and client controls require separate acceptance; this older source
does not establish their integrated exchange.
[Exact hosted storage evidence](evidence/native-oauth-refresh-storage-hosted-57da085.json).

The repaired `0eee494` hosted source passes all **117 native** contracts in
**344.09 seconds**, **257 extension / 55 browser** checks and packaging. Original
output confirms actual trusted native OAuth login/registered callbacks, encrypted
grant reopen, seven separate single-use refresh protocol modes and owned machine
root cleanup. Both artifact digests, all runtime/source-bundle bytes and packaged
views verify independently. This precedes the newer durable storage candidate and
does not prove automatic renewal, real authority or installed/rendered acceptance.
[Exact hosted evidence](evidence/native-oauth-refresh-hosted-0eee494.json).

The durable refresh storage candidate passes the complete rebuilt **117/117**
local native gate in **389.30 seconds**, with all **2464** inputs unchanged and
three focused contracts passing. Actual xlang3 SQLite verifies exclusive claims,
credential/quiescence fences, atomic ciphertext/receipt rollback/publication and
generation-bound restart retirement. Client source is unchanged; client suites
were not rerun. The required hosted inventory is **118**. Automatic renewal,
network coordination and client recovery remain unfinished. Both original focused
failures are preserved. [Exact local evidence](evidence/native-oauth-refresh-storage-local.json).
[Candidate and acceptance requirements](native-mcp-refresh-storage.md).

The native checkpoint `6afaaf7` passes the complete **116/116**
local native gate in **382.06 seconds**, with all **2462** frozen inputs unchanged.
The unchanged original managed-profile contract passes in the full gate, and
the two focused refresh contracts pass. Client source is unchanged from the
prior complete **257 extension / 55 browser** result; those suites were not rerun.
This proves the exchange primitive and scoped startup behavior, not durable or
automatic refresh, trusted native login, installed clients or full MCP parity.
[Exact local evidence](evidence/native-oauth-refresh-local.json).

The registered-callback hosted gate at `6708269` passed **115 of 116** native
contracts in **407.85 seconds**. The trusted contract stopped before native login:
test-certificate import into `CurrentUser\Root` rejected noninteractive use.
The downloaded artifact digest, exact inventory and original logs are verified.
Trusted acceptance remains unproved. The subsequent test-only repair uses the
elevated isolated runner's LocalMachine Root store, preserving exact CA ownership
and verified removal. Actual hosted execution of that repair remains pending.
[Original hosted failure](evidence/native-oauth-registered-hosted-6708269.json).

The native single-use refresh exchange primitive compiles and passes two focused
contracts, including independently decoded forms and real untrusted HTTPS contact
with zero HTTP dispatch. Durable refresh ownership/publication, automatic refresh,
running-owner coordination and client refresh controls remain unimplemented. The
first complete rebuilt 116-contract local gate passed **115** and failed managed
profile startup at its unchanged 60-second fixture deadline, in **376.95 seconds**.
The original run and retained fixture are preserved; the original focused repeat
failed at the same deadline. A longer-watchdog diagnostic completed, with a
57,196 ms cold start; it is not acceptance. Directory preparation now prepares
and pins each destination once; the rebuilt original focused contract passes in
75.40 seconds with its unchanged per-command watchdog. The following complete
gate passes as recorded above. The mandatory hosted inventory now
requires 117 contracts. [Original failure](evidence/native-oauth-refresh-first-failed.json),
[primitive and required production integration](native-mcp-refresh.md).
[Original focused startup pass](evidence/native-oauth-refresh-startup-local.json).

The console source `73ccf90` passed its complete hosted **114 native** gate in
**417.25 seconds**, **257 extension / 55 browser** suites and packaging. Independent
downloads verify both artifact digests, exact native contract inventory, all
**1860** runtime and **29** source-bundle files, **16** packaged-source comparisons
and **12** independently accepted browser assets. This package precedes the
registered-callback and refresh changes. No current installation, rendered UI or
trusted OAuth login is implied.
[Hosted console package evidence](evidence/native-oauth-cli-hosted-73ccf90.json).

The registered-callback candidate adds backend-owned OAuth callback path/port
configuration, exact exclusive binding and credential/agent authority checks.
The complete rebuilt **115/115** native gate passes in **388.83 seconds**, with
**2460** unchanged mapped inputs. Focused checks also pass **four** selected
contracts, including **13** real-socket callback cases and actual xlang3 SQLite
configuration/grant reopen. The unchanged clients retain their prior complete
**257 extension / 55 browser** results; their suites were not rerun here. The
strengthened trusted-login fixture compiles; hosted positive native OAuth
acceptance remains pending.
[Implementation and verification boundary](native-mcp-registered-callback.md).
[Exact local evidence](evidence/native-oauth-registered-local.json).

The trusted OAuth candidate passed the complete **115/115** local native gate
in **386.74 seconds**, plus **257 extension / 55 browser** checks, with **2459**
unchanged mapped inputs. The new independent HTTPS authority contract passes;
the actual native trusted-login fixture is compiled but deliberately not run
on the development PC. This is not successful native OAuth acceptance.
[Exact local evidence](evidence/native-oauth-trusted-local.json).

Trusted HTTPS OAuth acceptance is prepared as a mandatory isolated Windows
contract. The independent synthetic authority verifies S256, one-use codes,
resource/client/redirect binding, callback faults and authenticated MCP replies.
The compiled C++ fixture must acquire a grant through the actual native service,
persist/reopen it through xlang3 SQLite and use it through the production MCP
factory. The complete hosted inventory now requires **116** contracts. The
desktop certificate helper and hosted-only CMake option both reject locally;
the desktop's CurrentUser Root thumbprints remain unchanged. Successful hosted
native login is still unverified. [Acceptance boundary](native-mcp-trusted-acceptance.md).

The corrected `73ccf90` hosted view job passed all **257 extension / 55 browser**
checks. Independently downloaded evidence verifies its artifact digest, all
**45 tracked / 47 including vendor inputs**, unchanged inputs/assets and twelve
regenerated assets against exact accepted source bytes. This is scoped client
acceptance, with no native login or installed/rendered claim.
[Hosted view evidence](evidence/native-views-hosted-73ccf90.json).

Native MCP console controls pass the complete **114/114** local native gate in
**388.18 seconds**, plus **257 extension / 55 browser** checks, with **2455**
unchanged mapped inputs. The actual `xmind` console starts/observes/watches/cancels
the production C++ service with embedded-xlang3 encrypted grant metadata and an
independent untrusted TLS peer. Synthetic HTTP fixtures verify automatic native
revision selection, generated/published request identity, exact CAS admission,
terminal observation without cancellation dispatch, interactive detach, owned
NDJSON observation, strict metadata/HTTPS link validation and non-reflecting
errors. The original first compilation failure is retained. Trusted HTTPS
positive OAuth login, actual browser launch, registration, refresh/revocation,
running-owner coordination and installed/rendered acceptance remain unfinished.
[Exact evidence](evidence/native-oauth-cli-local.json),
[console contract](native-mcp-console.md).

The prior `f595205` hosted view job failed **before executing its Node suites**:
the three added test files and updated test totals were missing from the CI
inventory. The reviewed guard now requires all **45 tracked / 47 including vendor
inputs** and **257 extension / 55 browser** tests, without removing/skipping any
tests. Local full suites pass; the corrected hosted acceptance is recorded above.
[Original job log and exact failure](evidence/native-mcp-login-view-ci-f595205.json).

Native MCP login service and browser/VS Code Settings controls pass the complete
**113/113** local native gate in **378.02 seconds**, plus **257 extension / 55
browser** checks, with **2454** unchanged mapped inputs. The actual C++ async
service uses embedded-xlang3 encrypted grants and authenticated HTTP/View routes.
An independent self-signed HTTPS peer requires real TLS contact and zero HTTP
requests, native failed login without credential publication, duplicate request
identity/CAS rejection and retained terminal cancellation. The actual browser
adapter/shared client/controller also executes setup commands against that native
service. Synthetic DOM and VS Code API fixtures verify Settings controls,
ownership, HTTPS links, saved observation, lost-reply non-replay and view detach.
The first TLS-observation and VM import fixture failures are retained. Trusted
HTTPS positive OAuth login, registration selection, refresh/revocation, running
owner coordination, CLI login and current packaged/installed/rendered acceptance
remain unfinished. [Exact evidence](evidence/native-oauth-service-local.json),
[service and client boundary](native-mcp-login.md).

Native OAuth grant persistence/factory integration passes the complete
**112/112** local gate in **384.25 seconds**, with **2446** unchanged mapped inputs.
Actual xlang3 SQLite fixtures verify complete encrypted grants, a single-query
credential/metadata snapshot, atomic rotation/rollback/reopen, exact
resource/issuer/client/server binding, ciphertext relocation and malformed
record rejection, retired IDs and disabled-connector administration. The native
factory rejects missing/expired grants. An independent socket peer verifies one
valid discovery and zero later requests after expiry; the owner retires instead
of reconnecting/replaying. Authority digests change with OAuth credential
references/revisions. Refresh-token spaces survive native parsing and encrypted
storage. The first fixture-syntax build failure is retained; a final full build
includes all late source edits. Prior client source is unchanged and its 244/54
suites were not rerun here. Authenticated setup routes, trusted HTTPS login,
refresh/revocation, coordination of running owners and client login controls
remain unfinished. [Exact grant evidence](evidence/native-oauth-grants-local.json).

The native authorization-owner source `dd08604` passed the complete hosted
**110 native** gate in **332.01 seconds**, plus **244 extension / 54 browser**
contracts and packaging. Independent downloaded archive/VSIX verification checks
the **1860** runtime files, **29** source-bundle files, exact contract names and
packaged view sources. Original raw outputs and hashes are retained. This package
predates the newer callback receiver and encrypted OAuth grant integration and
has no installed/rendered current acceptance.
[Hosted authorization package](evidence/native-oauth-authorization-hosted-dd08604.json).

The native loopback callback receiver passes real-socket synthetic browser
redirects through the production authorization owner. The complete **111/111**
local gate passes in **378.16 seconds**, with **2443** unchanged mapped inputs.
A later two-file test-only strengthening and rebuilt callback contract passes
**12** cases in **0.99 seconds**, now checking port closure while the native
process/receiver remain alive. Production inputs/binaries remain unchanged;
the complete gate was not repeated after that fixture change. Exclusive Windows
binding, exact redirect/raw target/state/issuer admission, fixed non-reflecting
responses, denial, cancellation and deadline/owner expiry are verified within
that scope. Authenticated setup routes, encrypted OAuth grant persistence,
registration selection, trusted HTTPS login and client controls remain
unfinished. The first missing-include build failure is retained.
[Exact callback evidence](evidence/native-oauth-callback-local.json).

The configured native MCP HTTP and terminal-feed source `7007010` passed its
complete hosted **110-contract** native gate in **447.12 seconds**, plus **244
extension / 54 browser** contracts and packaging. Independent downloaded bundle
and VSIX verification checks all **1860** runtime files, the **29** source-bundle
files, exact contract names/gate hashes and packaged view bytes against that
commit. Original output and hash provenance are retained. This package predates
the newer authorization owner and callback listener and is not installed in the
running previews. [Hosted package evidence](evidence/native-mcp-http-hosted-7007010.json).

Native OAuth authorization components pass the complete **110/110** local gate
in **376.23 seconds**, with **2439** unchanged mapped inputs and the pinned native
xlang3 runtime, without executing CPython. The production private form encoder
is checked by an independent socket peer for exact resource/client/redirect/code
fields and RFC PKCE. Native fixtures cover random authorization requests,
callback state/issuer/redirect/expiry/replay, allowlisted denial, private token
parsing, cancellation and untrusted HTTPS rejection. Trusted HTTPS login,
callback listener/routes, registration selection, encrypted OAuth persistence,
refresh/revocation and browser/VS Code login remain unfinished. The earlier
244/54 client results apply to unchanged client source; those suites were not
rerun for this native-only checkpoint. Hosted and installed acceptance remain
separate. [Exact local component evidence](evidence/native-oauth-authorization-local.json),
[implementation boundary](native-mcp-http.md).

Native MCP HTTP/OAuth and terminal-feed checkpoint passed the complete local
**110-contract** gate in **371.74 seconds**, with **2438** unchanged mapped inputs,
the pinned native xlang3 runtime and no CPython interpreter/bridge. The complete
**244 extension / 54 browser** synthetic suites also pass. Source and binary
hashes, exact contract names and original detailed outputs are retained.
[Complete local acceptance](evidence/native-mcp-oauth-local.json).
The native configured HTTP owner/approval registry passes actual official SDK
JSON/SSE file effects, denial with zero dispatch, duplicate rejection, lost-reply
uncertainty and xlang3 SQLite restart. Native metadata/challenge fixtures,
separate Basic/Bearer response headers, typed 401 retirement, cancellation,
deadline and untrusted-TLS rejection pass. No positive OAuth HTTPS discovery,
registration/login/token exchange, refresh or graphical setup is delivered.
[Implementation and remaining scope](native-mcp-http.md).

The older hosted `298da66` view gate failed before the complete native suite:
after human input, no committed event arrived incrementally. Browser and VS Code
could stop their owned feed when a faster command snapshot already showed
completion. Both now retain the selected feed through its native terminal
acknowledgement. Original prior-source regressions reproduce both defects; all
**244 extension / 54 browser** synthetic contracts pass. Selection/disposal still
retires observation. The actual native view contract passes in **78.32 seconds**,
including real sidebar creation, exact approval/receipt/disk checks and terminal
stream cursor validation. This exercises the production extension/controller with
VS Code API fixtures; it is not rendered or installed IDE acceptance.
[View regression and hosted failure](evidence/native-browser-terminal-drain-candidate.json),
[native view follow-up](evidence/native-mcp-oauth-local-view-drain-recheck.log).

The first complete local native attempt passed **108/110** in **359.55 seconds**.
It retained a 60-second first-profile startup timeout and the sidebar race.
Fresh profile acceptance subsequently passed in **74.42 seconds** with no timeout
increase or native profile-code change. The original startup timing failure is
not attributed to a proven root cause. The sidebar check now waits for the actual
terminal protocol acknowledgement and exact final cursor before retaining its
independent incremental-event/history/effect assertions. The complete revised
gate subsequently passed as recorded above; the original startup timing failure
still has no proven root cause. Hosted packaging and installed/rendered current
coding acceptance remain pending. Earlier source-only statements below are
historical checkpoints superseded by this scoped local result.
[Original failed gate](evidence/native-mcp-oauth-local-first-failed.json),
[profile/view first follow-up](evidence/native-mcp-oauth-local-profile-view-first-recheck.log).

The later hosted `ad5a363` also failed the same early browser incremental-event
assertion before reaching its complete native gate. It predates both terminal
feed-retention fixes. Its original log/provenance are retained separately;
the current local result is not advertised as a hosted or installed pass.
[Hosted failure provenance](evidence/native-browser-terminal-drain-ad5-failure.json).

Native early view acceptance failed at `e6032d9`: its actual browser resume
reported `Unowned graph event`, so the full native gate was not reached. Original
hosted failure/provenance/job output is retained. The browser snapshot fetched
children before events; a newly committed child could be missing from that older
owner list. Source now reads events before fresh root/child metadata, and refreshes
terminal ownership/operations before displaying final events. Ownership rejection
remains intact. New deterministic initial/terminal fixtures reproduce both errors
against the original source and pass the correction; an unowned event still fails.
Full **243 extension / 53 browser** synthetic suites pass with all **2436** tracked
inputs and 12 generated assets unchanged. Actual native correction acceptance,
current MCP compilation and installed/rendered coding remain pending.
[Failure and candidate evidence](evidence/native-graph-snapshot-order-candidate.json).

Native MCP HTTP configuration/factory candidate: production agent execution,
idle context preparation, graph tools and admin discovery now select a shared
native owner from immutable stdio/HTTP settings. HTTP endpoint and encrypted
server bearer references bind credential purpose; native authority includes
transport, endpoint, reference and actual credential revision. Public server
metadata reports the transport without destinations/keys. Admin provisioning
uses the registered `BEARER` target and retains catalogue-reflection checks.
The official HTTP SDK effect assertion now goes through persisted configuration,
encrypted credential resolution and this production factory. Existing native
configuration/authority contracts add URL/mixed-field/plaintext/scope rejection,
endpoint-change binding, pre-connect cancellation/deadline and SQLite reopen.
Whitespace/source and Node fixture syntax checks pass; native acceptance remains
pending. OAuth, broader HTTP lifecycle and graphical setup remain unfinished.
[Current implementation boundary](native-mcp-http.md).

Native MCP HTTP owner candidate: `McpHttpClient` now implements the same private
dispatch interface as stdio, using incremental JSON/SSE decoding and shared tool
description/result validation. Negotiation, per-tool header filtering, immutable
catalogue checks, exact request correlation and interruption attribution are in
native source. The existing effect contract is expanded with a pinned official
HTTP SDK peer: real JSON/SSE disk effects after approval, denial with zero calls,
duplicate rejection, one lost-reply effect and actual SQLite restart checks.
The peer independently checks bytes/counts. Node syntax/whitespace and a separate
SDK-only schema/response-shape probe pass; C++ compilation and execution remain
pending. Product configuration/factory, OAuth and broader HTTP lifecycle remain
unfinished. [Exact scope](native-mcp-http.md).

Native MCP HTTP POST candidate: the native async WinHTTP layer now sends
request/schema-derived headers and exposes bounded response metadata/bytes
for JSON/SSE and authentication challenges, with a conservative send boundary.
Existing provider behavior retains its selected-header/media rules. Real-socket
synthetic peer assertions are added to the existing transport contract for raw
arguments, both media, status/session/auth metadata, acknowledgement shape,
exact dispatch counts, cancellation, deadlines and certificate rejection.
Node syntax/whitespace checks pass; native compilation/execution is pending.
Connected MCP ownership, decoding, configuration/factory and OAuth remain
unfinished. [Scope](native-mcp-http.md).

Native browser streaming accepted at `977e945`: the full hosted **110 native
contracts** passed in **356.85 seconds**, with **241 extension / 49 browser**
fixtures and actual browser/native integration. The production cookie client,
shared reader/subscription and native gateway now have actual paused
detach/reattach, checkpoint-bound human input, committed incremental events,
two-stream capacity/command fairness and process-exit revocation acceptance.
Independent verification checked the downloaded bundle/runtime/VSIX hashes,
all 16 host/view source mappings and generated browser assets. Later clean EOF,
gateway restart, actual extension controller and approved creation assertions
remain pending, as do fresh installed/rendered coding and live inference.
[Scope](evidence/native-event-stream-hosted-977e945-scope.json),
[package verification](evidence/native-event-stream-hosted-977e945-package.json).

Native MCP HTTP metadata candidate: the actual wire library now validates and
projects modern request headers and declared parameter headers, including
exact safe integers, nested paths, Unicode/control encoding, absent/null values
and case-insensitive name uniqueness. Its existing native contract has bounded
fixtures; compilation/execution is pending. HTTP networking, discovery filtering,
OAuth and effect acceptance remain unfinished. The design now distinguishes
modern request-scoped HTTP from older session/resumption semantics.
[Implementation and remaining acceptance](native-mcp-http.md).

Native MCP transport interface candidate: production `McpToolRegistry` now
accepts a transport-neutral `McpToolClient`, implemented by the existing stdio
binding. Dispatch stays private to the same durable approval/effect registry.
Common failures preserve possibly-sent/request/response attribution. The actual
MCP effect contract is updated to use the abstract interface and require private
dispatch/shared deadline/noncopyable ownership at compile time. Whitespace/source
checks pass; native compile/execution is pending. HTTP/OAuth remain unavailable.
[Pinned protocol, implementation boundary and remaining acceptance](native-mcp-http.md).

Production sidebar approved-creation acceptance prepared: a second actual
native graph requires human input, then a `create_file` proposal. The host
fixture runs the production comparison path and requires absence before
approval, rejection of a retired sidebar's decision, current-sidebar approval,
exact Unicode disk bytes and persisted receipt hash/size, one child and terminal
history. Syntax/whitespace checks pass; actual native execution is pending.
Model requests remain excluded from this acceptance; installed/rendered coding
and live inference are separate requirements. [Scope](native-event-stream.md).

Actual production VS Code controller acceptance prepared: the native view
contract now imports a host fixture that executes production `extension.js`,
`WorkspaceBackend`, client and subscription against the real compiled backend.
It requires paused replay, sidebar disposal/reopening, rejection of a retired
view's human input, current checkpoint-bound input, exactly one actual tool
child and persisted terminal history. Native transport is instrumented for
observation only; no domain replies are fabricated. Only syntax/whitespace
checks pass so far. Native execution and installed/rendered IDE acceptance
remain pending. [Acceptance scope](native-event-stream.md).

Clean frame-boundary EOF recovery candidate: the shared committed reader still
rejects a missing native end acknowledgement, but now marks a clean connection
EOF as transport unavailability. The subscription retries at the last accepted
cursor with its existing three-attempt bound; it sends only stream GETs.
Incomplete/malformed frames, identity/authentication and consumer failures stop
explicitly. Complete **243 extension / 50 browser** synthetic suites pass with
all 2421 tracked inputs unchanged. The real native view contract now also
prepares a browser-adapter restart at the same origin/cookie, requiring a fresh
validated observation at the preserved cursor while the actual graph stays
paused. That new native assertion is unexecuted.
[Candidate and original evidence](evidence/native-event-stream-eof-candidate.json).

Native SSE transport accepted at `7a148ff`: the isolated hosted build passed
all **110 native contracts** in **371.87 seconds**, then **224 extension /
43 browser** fixtures, actual browser/native integration and package checks.
The executed native view contract compares completed real tool-graph run,
graph and owned-tree streams with persisted REST records, verifies
Last-Event-ID replay/cursor rejection and the scoped browser-cookie feed.
Independent artifact verification checked all 29 bundle/1860 runtime file
digests, the exact native contract set, 16 packaged host/view source mappings,
generated browser HTML and vendor copies. Native source compiled; no local
native execution or installation change occurred. Production reader,
subscription/controller adoption and active-feed/capacity/revocation assertions
were added later and remain unverified natively. Fresh installed/rendered
streaming remains pending.
[Exact scope](evidence/native-event-stream-hosted-7a148ff-scope.json),
[verified package](evidence/native-event-stream-hosted-7a148ff-package.json).

Full webpage streaming fixture: the complete emitted classic-script order,
cookie connection bridge, actual shared reader/subscription and renderer now
run together in JSDOM. Synthetic committed frames render live text and exact
normalized usage, remain idle without periodic run polling, replace live output
with one persisted terminal response and detach without sending cancellation
or admission. Complete **241 extension / 50 browser** synthetic suites pass
with all 2421 tracked inputs unchanged. This is DOM fixture evidence, not a
native backend or installed/rendered browser acceptance claim.
[Scope and original evidence](evidence/native-event-stream-browser-page-candidate.json).

Browser cookie-stream correction candidate: the page's session client previously
overrode JSON requests only, so its inherited SSE method attempted bearer-token
acquisition and failed. `BrowserSessionClient` now supplies same-origin cookie
transport for both JSON and SSE while retaining the shared committed parser,
ownership checks and subscription. Editor clients retain protected bearer
transport. All **241 extension / 49 browser** synthetic checks pass with all
2421 tracked inputs unchanged. New fixtures exercise the production cookie
client and parser, UTF-8 delivery, no bearer acquisition/export, pre-abort,
invalid scope and expired-session failure. The real native controller acceptance
now uses that actual cookie client through the browser adapter; it remains
unexecuted. [Candidate and original evidence](evidence/native-event-stream-browser-cookie-candidate.json).

Actual native/browser-controller stream assertions are prepared in the native
view contract: a real human/tool graph stays paused during observation, survives
view disposal, replays after reattachment, accepts explicit checkpoint-bound
input and completes its actual file read. Assertions require incremental
committed frames, unique displayed sequences and exact persisted terminal
history. Only syntax/whitespace checks have run; this acceptance remains pending.
The separate local benchmark is still live, so no local native launch is claimed.
The same contract now prepares actual two-stream capacity/command-fairness,
reader detach and view-process-exit reauthentication checks while a real graph
remains paused. Those additional checks are also unexecuted.

VS Code subscription adoption candidate: the actual extension host now uses
the same scoped committed-event subscription as the browser. Periodic run
polling is removed. Event-triggered snapshots preserve history, approvals,
plans and owned children; graph child transcripts refresh after direct events.
Direct streamed delivery awaits the webview acknowledgement. Selection,
workspace/view changes and disposal retire observation without cancelling runs.
All **241 extension / 47 browser** synthetic checks pass with all 2421 tracked
inputs unchanged. New host fixtures cover delivery and coalesced metadata,
retired callbacks and graph child transcript refresh; historical-run selection
now follows the active feed. Native streaming and fresh installed/rendered
acceptance remain pending.
[Candidate and original evidence](evidence/native-event-stream-vscode-adoption-candidate.json).

Browser subscription adoption candidate: the browser controller now observes
the shared committed-event feed, with no periodic HTTP timer. Read-only
run/graph/approval/plan/history snapshots refresh on observation, committed
events and terminal end; event-triggered refreshes coalesce for 100 ms. Serialized
snapshots discover new child ownership after an older snapshot, and overlapping
REST/SSE records render once. Selection changes and disposal retire observation
without cancelling execution. All **238 extension / 47 browser** synthetic
checks pass with all 2421 tracked inputs unchanged. Actual native streaming,
VS Code adoption and fresh installed/rendered acceptance remain pending.
[Candidate and original evidence](evidence/native-event-stream-browser-adoption-candidate.json).

Shared subscription lifecycle candidate: connection rotation resumes the
delivered cursor, and root/conversation/origin/generation changes abort only the
old observation and refuse its delayed events/outcomes/retries. Busy/transport
retries are bounded; authentication, protocol and consumer failures stop
explicitly. All **238 extension / 43 browser** fixture checks pass with all
2421 tracked inputs unchanged. The real native contract now includes production
subscription replay, but it has not executed. Both UI controllers still poll;
adoption and installed/native acceptance remain pending. The accepted installation
is unchanged. [Candidate and exact verification scope](evidence/native-event-stream-subscription-candidate.json).

Shared event reader candidate: `BackendClient.eventStream` validates root,
conversation, scope, committed IDs and owned-child metadata, decodes split UTF-8
and CRLF, preserves consumer-acknowledged cursors and detaches on abort/protocol
failure without sending commands or fallback requests. All **232 extension /
43 browser** fixture checks pass with all 2421 tracked inputs unchanged.
Production-reader replay/resume checks are added to the real native view contract
but have not run yet. Native integration, subscription/controller adoption and
installed/rendered streaming remain pending; the accepted installation is
unchanged. [Candidate scope and original evidence](evidence/native-event-stream-client-candidate.json).

Committed-event transport candidate: native run/graph/owned-tree SSE endpoints,
scoped cursor replay, bounded connection/stream leases and incremental native
view/browser forwarding are implemented. The complete local **224 extension /
43 browser** fixture suites pass with all 2421 tracked native/tool/view inputs
unchanged. The browser fixture observes a frame before the peer finishes and
detaches without sending cancellation. New real native replay/cookie assertions
are prepared but unexecuted. Current UI controllers still poll; native acceptance,
shared client/controller adoption and installed/rendered streaming remain pending.
The accepted `689404e` installation is preserved.
[Protocol, implementation and remaining acceptance](native-event-stream.md).

Hosted source `689404e` passed **all 110 native contracts** in **275.04 seconds**,
then **224 extension / 41 browser** checks, actual browser/native integration,
runtime staging and VSIX verification. The real production editor host connected
through the native view, wrote sessions, ran a native tool graph, shared console
history, enrolled browser access and verified process-exit revocation and same
profile/history after host reload. This is actual native integration with a
fixture VS Code API, not installed/rendered editor acceptance or live inference.

Independent downloaded-artifact verification checked the exact registered and
passed contract sets and original gate hashes, all **29 bundle / 1860 runtime**
file digests, **16** committed host/view source mappings, generated browser HTML
and vendor copies. The VSIX has **1897 entries**, five native files and 1830 pure
source files. Original logs/manifests and the verifier's initial source-map error
are recorded. No local native execution or current installation acceptance is
claimed. Fresh installed/rendered coding, declarative/code authoring,
workers/IPC, profile binding and full parity remain required. Native/browser-
controller SSE acceptance now passes locally; installed/rendered acceptance
remains open. No migration layer is implemented.
[Passed gate, original evidence and independently verified package](evidence/native-view-hosted-689404e-package.json).

That exact VSIX is now installed into a fresh isolated VS Code profile. All
**1895 installed extension files** were compared with the verified archive;
only VS Code's added package `__metadata` is excluded from structural manifest
comparison. Every native runtime byte matches. Existing installations were
preserved. This is installation/file evidence only: no local native runtime was
launched while the separate benchmark parent remains live. Rendered interaction
and actual coding acceptance in this profile are still pending.
[Fresh installation and complete file comparison](evidence/native-view-hosted-689404e-fresh-install.json).

Hosted source `0b96974` compiled and reached native view readiness, then failed
the first actual session write with HTTP 400 `Invalid JSON request`. The owner
fix therefore passed that startup boundary, but the native contract did not pass.
Source inspection found that forwarding ran before cpp-httplib read POST bodies.
The candidate now keeps access checks in pre-routing and forwards from regular
GET/POST handlers. Exact Unicode/chunked writes and malformed/oversized rejection
are added to the real native contract. Syntax/whitespace checks pass; native
rerun, the full 110-contract gate and fresh installed UI acceptance remain pending.
No migration layer or local native execution was added.
[Original native failure and candidate scope](evidence/native-view-body-forwarding-candidate.json).

The corrected hosted build reached real view startup and failed its strict owner
check. Original runner provenance confirms that the default owner differs from
the current user. The host now chooses an unused name and leaves leaf creation
to native's explicit-user protected storage. All **224 extension / 41 browser**
fixture checks pass, including collision and alias refusal; the real native test
now checks that the host leaves the leaf absent before launch. Native rerun,
the full 110-contract gate and installed acceptance remain pending.
[Original failure, before-fix regression and candidate evidence](evidence/native-rendezvous-owner-candidate.json).
The same pushed source passed hosted **224 extension / 41 browser** checks with
all 44 source/vendor files and 12 generated assets unchanged. Downloaded asset
bytes match the original gate digests. This does not prove native startup.
[Original hosted gate and independent artifact checks](evidence/native-rendezvous-owner-hosted-views.json).
The older queued `da4f6e1` build then repeated the same owner failure; it does
not contain this host fix. The corrected `0b96974` native run has now started.
[Original older failure and corrected run identity](evidence/native-rendezvous-owner-older-confirmation.json).

The local native launcher now detects the actual Python benchmark parent as well
as its workers, and rechecks before configure, compile and contracts. The previous
guard missed live parent PID 8220; a real launch after the fix exited with deferral
code 3 before creating a build directory. The benchmark was not changed and no
native test pass is claimed. Isolated native CI continues independently.
[Actual parent observation and launcher refusal](evidence/native-benchmark-parent-guard-local.json).

Hosted source `faab16f` compiled and finished its full native test run with
**107/110 passed** in 310.95 seconds. The same license-staging and removed editor
upgrade-API failures blocked it; the actual local-view regression did not run.
Those causes were already corrected in `9f73bdc`. The corrected `645d76f` run
has started, with the real view test scheduled before unrelated test compilation.
Native readiness and the complete gate remain unaccepted.
[Original older result and exact current-run identity](evidence/native-view-preflight-hosted-failed.json).

The existing earlier browser checkpoint was inspected and its history refresh
button exercised without submitting a model request. Saved transcript/usage
and provider choices were visible, but refresh required connecting again.
Cached display is not proof of a usable backend connection or acceptance of
the current native adapter. No credential was entered or authentication bypassed.
[Actual browser observation and original screenshot](evidence/browser-existing-refresh-observation.json).

Protected profile creation now explicitly selects the current Windows user as
owner, matching the existing verification rule even when a token's default
owner is a group. The DACL and refusal of foreign-owned existing storage are
unchanged. Windows descriptor parsing confirms the supplied owner and unchanged
access entries; native compilation and execution remain pending. This is not
established as the earlier view-readiness failure's cause.
[Candidate source and scoped descriptor probe](evidence/native-profile-explicit-owner-candidate.json).

External attachment now rechecks workspace trust and host location after native
workspace verification, before accepting the connection. The regression failed
on the previous code; all **221 extension fixture checks** pass after the fix.
[Failure and complete rerun](evidence/view-attach-trust-local.json).
The same pushed source passed the complete hosted **221 extension / 41 browser**
gate with all 44 source/vendor files and 12 generated assets unchanged.
[Downloaded original evidence and independently checked asset digests](evidence/view-attach-trust-hosted.json).
This does not establish native or installed-editor acceptance.

The first hosted native-view gate completed with **107/110 passed**. The two
local profile/view contracts stopped during packaging because the pinned pure
source checkout has `LICENSE`, while the fixtures requested `LICENSE.txt`.
The handoff contract then invoked the removed editor upgrade API. The candidate
fix uses the actual source license, removes that obsolete editor scenario while
retaining native owner-handoff safety assertions, and invokes the browser test
through `xmind.exe serve`. No migration flow is introduced. Syntax checks pass;
the complete native rerun and installed acceptance remain pending.
[Original hosted failure and scoped fixes](evidence/native-ci-current-format-candidate.json).

First-sidebar reconnect fix: changing folders before the first view exists
now preserves the pending open intent and reopens the latest workspace. The
regression failed on the previous code and the complete **220 extension fixture
checks** pass after the fix. No run cancellation is issued.
[Original failure and complete rerun](evidence/first-sidebar-reconnect-local.json).
Native adapter/installed acceptance remains pending.

Editor startup selection fix: a folder change during pending preparation now
waits for stale cleanup and connects the latest root; trust revocation prevents
new native startup. The complete **219 extension fixture checks** pass.
[Scoped source and original output](evidence/view-startup-selection-race-local.json).
Native view readiness and installed acceptance remain pending.

Native editor-view adapter candidate: the host now delegates startup/storage
to C++ and scopes saved UI choices to the native workspace/profile. All **217
extension fixture / 41 browser fixture** checks pass. Native acceptance is
**pending**: the first probe timed out preparing the backend; the second started
it but the view failed before readiness. Diagnostic reporting is prepared.
Further native builds/probes were deferred for a separate live xlang3 performance
run. No working installed connection, native 110-test gate or UI rendering is
claimed. [Contract and remaining acceptance](native-view-adapter.md).

Native automatic console profiles now start or reconnect to one persistent
workspace backend without a manual port/token setup. The controller verifies
the actual native process and authenticated workspace, seals its rendezvous
with user DPAPI and preserves the same SQLite sessions/history after restart.
A console exit leaves its backend alive. The full **109 native contracts** and
**10 focused checks** pass with frozen source. Runtime inventories remain
immutable while ancestor control records can update atomically.
[Profile contract](local-profiles.md) · [Actual local evidence](evidence/native-local-profile-local.json).
This is native console acceptance; installed VS Code/browser controller adoption,
worker IPC, authoring and complete parity remain pending. No migration is needed.

Native console workspace pinning now accepts `--workspace DIR`, checks the
selected OS directory identity and authenticated backend authority before each
request, and sends the pinned pair in agent/graph admission. The actual native
server rejects a stale-authority race before creating a run. The complete local
**108 native contracts** and eight focused checks pass with frozen source.
The first focused test had an incorrect stderr assertion; its original log is
retained and the corrected test checks the native HTTP status and stored state.
[Console connection evidence](evidence/native-console-workspace-local.json).
Automatic profile discovery/start was pending at that checkpoint; the native
controller is accepted above.

The verified **0.1.5** VSIX is installed in a fresh dedicated VS Code profile;
its runtime and 25 client/browser assets match their accepted hashes. First-run
sign-in is awaiting user handling. No fresh rendered-sidebar or installed-model
acceptance is claimed. The older installation is intact.
[Installed file evidence](evidence/vscode-unified-fresh-installed-files.json).

The single-format package checkpoint **6adb9a1** removes the client legacy-profile
migration path and accepts only `xmind.exe` plus xlang3 dependencies. The complete
local **107 native / 225 extension / 41 browser** suites pass. The first native
attempt exposed an outdated PowerShell test build path; the corrected complete
rerun passed in **210.08 seconds**, with **2,414 frozen inputs**.
[Source and original logs](evidence/native-unified-package-local.json).

The new **0.1.5 VSIX** passed exact verification of **1,935 archive entries**,
including **1,869 pure-source files** and **23 license files**. Its actual packaged
`xmind.exe` completed two real OpenAI runs in a fresh SQLite profile: five
approved effects, one denied effect, actual provider usage and exact multiline
history across restart. No old profile or installation was touched.
[Package and live evidence](evidence/native-unified-package-release.json).
Fresh installed/rendered VS Code acceptance, declarative/code authoring APIs
and agent-worker integration remain pending. Native/browser-controller SSE
integration passes locally; rendered/installed UI acceptance remains open.
Native console bootstrap is accepted
in the later checkpoint above.

The primary **`xmind.exe`** now executes the existing native backend, console,
admin and private schema handlers through one program. The complete local
**107-contract gate passed in 233.55 seconds** with **2,393 unchanged mapped
inputs**. The four unified contracts cover actual patch approvals/effects,
graph execution, MCP subprocess effects, Unicode configuration paths and
embedded-xlang3 SQLite restart. Synthetic provider fixtures remain explicitly
scoped. The first focused attempt exposed a Windows Unicode config-open defect;
all eight admin/server readers were corrected and the original failure retained.
[Complete native evidence](evidence/native-unified-local.json).

The exact tested `xmind.exe` hash also completed **two real OpenAI gpt-6.1-sol
console runs**: literal multiline mixed add/update/move/delete and a partial
two-file patch with the second file denied. Five actual approved effects and
one denied effect matched native receipts and disk hashes. Both prompt/history
records survived an actual backend restart unchanged, and six actual provider
usage records were retained. [Live console evidence](evidence/native-unified-cli-live.json).

The pinned xlang3 SDK independently passed its official shared-memory script,
parallel-client and native C++ smoke without a rebuild or CPython. This is a
transport prerequisite, not integrated agent-worker or performance acceptance.
[IPC evidence](evidence/xlang3-ipc-foundation-local.json) and
[worker design](native-ipc-worker-design.md).

The installed client still uses its existing 0.1.4 runtime generation. The new
package uses one `xmind.exe` and will be validated with a fresh local profile;
legacy-profile migration is excluded by the product decision. Fresh installed
acceptance and automatic console bootstrap remain pending, along with
declarative/code authoring APIs and agent workers. Native/browser-controller
SSE integration now passes locally; rendered/installed UI acceptance remains
open.

The CLI multiline/composer source checkpoint passed the complete **103 native
contracts in 215.71 seconds**, with 2,390 unchanged mapped inputs, plus **222
extension / 41 browser** fixture tests. Native contracts exercise composed
agent/graph prompts and discard boundaries; provider responses are synthetic.
The first complete attempt also passed 103 tests but its source-freeze wrapper
correctly rejected a concurrent extension version change; a fresh gate passed.
That earlier source-only gate did not establish installed 0.1.5 rendering or
live-provider multiline acceptance; the later live console evidence above is
separate. Installed 0.1.5 rendering remains pending.
[Frozen evidence](evidence/native-compose-local.json).

The next product direction is [Agent Runtime + Coding Harness + Model
Gateway](runtime-product-design.md), with one primary `xmind` executable. The
document distinguishes existing native execution from pending authoring,
streaming, bootstrap and worker delivery.

The audited **0.1.4 package** now runs in the existing TestProj VS Code profile,
preserving all 22 messages and seven runs through the native upgrade. An actual
OpenAI two-file patch opened comparison before effects, required two separate
UI approvals and completed with matching native receipts, disk hashes and
provider metrics. The packaged interactive CLI separately passed real mixed
add/update/move/delete and partial denial, plus exact restart history. The
archive audit checked 1,938 entries and 1,901 runtime files; the versioned
client suites passed 222/41. The installed 0.1.4 composer and CLI retain their
older behavior; the source fixes above have not been installed there. Older
browser-preview coverage and full coding parity remain incomplete.
[Package and live/installed evidence](native-file-patch.md).

Native multi-file patch execution now passes the complete **103-contract local
gate** in 211.24 seconds after correcting the Windows handle-relative rename
call and a missing synthetic routing asset. All 2,390 mapped source inputs
remained unchanged. Separate hosted client acceptance passed **222/41** tests
with verified artifact/source bindings. The installed VS Code 0.1.3 client also
completed a fresh real OpenAI workspace read, preserving the earlier failed
conversation. This does not install or accept the new patch runtime; packaging,
rendered patch approvals and real-provider patch execution remain pending.
[Native evidence and limits](native-file-patch.md), [client evidence](patch-review.md),
[installed fresh-read recovery](evidence/vscode-sidebar-read-live.json).

Native discovery/content search now share hierarchical local ignore rules,
scoped Git repository boundaries, negation, source priority and explicit positive
glob overrides. All **98 native contracts passed in 209.78 seconds**, with 637
unchanged inputs, plus **215/39 client** contracts. A real OpenAI search returned
only two allowed paths and retained history across an xlang3/SQLite restart,
without mutation operations. Actual Git corpus decisions match; the first
ripgrep comparison exposed two retained dialect differences. Global ignore
configuration, full dialect/search parity and installed writing remain incomplete.
[Behavior and scope](native-ignore.md), [exact evidence](evidence/native-ignore-local.json).

Native `glob_files` supports recursive wildcard, brace, character-class and
Unicode path discovery through retained workspace handles. Ordinary agents,
read-only delegated children and direct graph tools share the implementation.
The corrected source passed **98 native contracts in 208.70 seconds**, with
635 frozen inputs, plus the unchanged **215/39 client** gate. Actual OpenAI
discovery found and quoted four fixture paths, excluded private configuration
and preserved history across an xlang3/SQLite restart. An actual depth-boundary
fixture passed after moving the enumeration buffer out of recursive frames.
The initial four stale synthetic-catalogue failures are retained. Gitignore
rules, escaped metacharacters and full search/editor parity remain incomplete.
[Behavior and limits](native-glob.md), [exact evidence](evidence/native-glob-local.json).

Native text-file pages support 1-based offsets, up to 2,000 lines, explicit
Unicode clipping and resumable output bounds for files up to 64 MiB. All
**98 native contracts passed in 207.52 seconds**, with 633 mapped inputs
unchanged, alongside **215 extension / 39 browser** contracts. A real
`gpt-6.1-sol` run read the exact final two lines of a 4.29 MB file, quoted their
Unicode text and retained the same history after an xlang3/SQLite restart.
No mutation operations were created. The initial live harness setup failure is
retained; full read parity and installed/editor writing remain incomplete.
[Behavior and limits](native-file-pages.md), [exact local evidence](evidence/native-file-pages-local.json).

Workspace-bound editor selection now appends exact, labelled buffer context
to the existing sidebar draft without a question popup or automatic submission.
Initial view readiness, stale editor/workspace state and retained-view
reconnection have accepted source contracts. All **215/39 view tests** passed;
actual filesystem metadata checks and the **0.1.3 VSIX** audit passed separately.
The native runtime was unchanged at this checkpoint. Installed/rendered
acceptance remains pending for a fresh current-format installation; no migration
is required.
[Behavior, artifact and limits](vscode-editor-selection.md).

The local **0.1.2 VSIX** packages the accepted native file/folder-creation and
Responses-diagnostic source. Complete archive/inventory and ten host/view
source checks passed, as did 200/39 versioned client tests and a qualified
packaged-server/xlang3 SQLite/session/retirement smoke. It contains no private
state or CPython executable/extension/bytecode. The existing installed profile
and pending migration were not replaced; installed writing remains unverified.
[Artifact, hashes and scope](native-parent-package.md).

Native file creation now supports approval-backed missing parent folders,
with existing ancestor guidance, disclosed directory effects and create-new
checks. Real OpenAI CLI creation/editing and both edit/creation denial passed;
denied creation left its folders absent. The final **98 native / 200 extension
/ 39 browser** gates passed. The stale committed CI view count was corrected,
and earlier attempts remain retained. Installed/rendered folder approval
acceptance remains separate.
[Implementation, live receipts and limits](native-parent-creation.md).

Real OpenAI native CLI file creation, editing and denial passed with actual
approval operations, filesystem hashes and six supplied response-usage events.
Responses failures now retain explicit native classifications and validated
usage, with actionable shared client messages. All **98 native, 199 extension
and 39 browser tests passed**. The earlier unclassified provider failure and
corrected acceptance-driver errors are retained. Installed VS Code writing
and its migration remain separate and unverified.
[Implementation, live evidence and limits](native-responses-failures.md).

Native legacy publication failure after real termination now has accepted
rollback/recovery coverage and clearer native/client diagnostics. All
**98/198/39 local tests passed**. Actual failure, absent ticket, retained session
and a separate operator preparation/activation were verified; rollback-call
failure and installed writing remain unverified. The installed migration dialog
still awaits the user's confirmation.
[Fault scope and evidence](native-legacy-publication-fault.md).

The verified **0.1.1** migration VSIX is installed in the existing normal
TestProj profile. Fresh 98/198/39 gates, independent archive audit and actual
complete-runtime smoke passed. Existing transcript/model/usage reconnected
without another key prompt. The old runtime was repaired after VS Code cleanup
using its verified copy. This historical migration package is superseded by
the current-format fresh installation; no migration confirmation is required.
Actual create/edit acceptance remains pending.
[Package and installed evidence](native-legacy-package.md).

Legacy migration now has native writer-fenced operator termination and managed
extension adoption/recovery. Complete local checks passed **98 native, 198
extension and 39 browser tests**. Actual server/admin/adapter fixtures retain
the same database and saved sessions, consume the exact ticket on activation,
recover lost stop output without replay and reconnect on host reload. Installed
migration and rendered coding remain pending.
[Implementation, evidence and limits](native-legacy-stop.md).

Native legacy preflight now authenticates the workspace and verifies actual
listener PID/birth, executable/hash and database/workspace command line before
migration. All **98 contracts passed**, with 631 mapped inputs unchanged.
It leaves the source running and creates no ticket. Installed shutdown/adoption
and rendered file writing remain pending.
[Exact scope and evidence](native-legacy-preflight.md).

Stopped legacy-owner preparation and qualified activation passed all **98 native
contracts**, with 629 mapped inputs unchanged. Actual administration/server
fixtures preserve the same database and keep admission closed until activation.
The native SQLite import repair addresses the retained hosted handoff failure.
View sources are unchanged from the previous 197/39 passing checkpoint.
Product legacy shutdown, installed upgrade and rendered file-writing acceptance
remain pending. [Scope and exact evidence](native-legacy-owner.md).

Provider/conversation switching now has catalogue recovery in both adapters,
with source-bound hosted **175/35** acceptance. Tests cover retired native
selection acknowledgements, a second switch during recovery and the exact
revision-bound submission fields. The tested browser asset is published;
rendered recovery and updated VSIX installation await local validation after
the independently running SDK benchmark clears.
[Recovery scope and retained failures](model-catalogue-recovery.md).

Compact-height browser/IDE styles now preserve conversation space. The actual
319 × 431 pane gained 96 pixels of history space; long drafts and expanded
context controls scroll without collapsing it. Complete 173/33 frontend suites,
independent VSIX verification and persistent-profile installation passed.
Native source and runs stayed unchanged. Rendered IDE interaction and the
separate provider/session catalogue recovery issue remain pending.
[Compact layout evidence](compact-sidebar.md).

The local browser preview now runs source `60475f8` with its matching assets
and paired xlang3 SDK. The upgrade preserved existing API records, configuration
and browser authentication. A disposable schema 10 → 12 → 10 fixture and fresh
model-free native/browser integration passed. All four configured providers
completed real rendered sidebar runs with supplied metrics. Refresh retained
the session/provider/model without reconnecting or admitting another run.
Desktop right-sidebar layout was checked; compact-height rendering and normal
VS Code rendered interaction remain pending.
[Browser upgrade, live UI evidence and limits](browser-preview-upgrade.md).

The native OpenAI model eligibility policy passed **89/89 contracts in 172.73
seconds**, with all 564 inputs unchanged. Discovery, enrollment and startup now
use explicit route/model declarations; current GPT-6 tool restrictions and
reasoning requirements are enforced before publication or execution. The 39
frontend inputs are unchanged from the 173/33 checkpoint. The subsequent native
live check passed all four configured providers, including GPT-6.1 Sol, and
OpenAI discovery returned 20 eligible IDs. The new package passed ZIP integrity
and actual installed VS Code opened-folder binding, then was installed in the
normal profile. Rendered VS Code interaction remains pending; the subsequent
browser upgrade and actual sidebar runs are recorded above.
[Model eligibility scope](native-model-eligibility.md).

The latest native provider checkpoint passed real workspace reads and exact
replies from OpenAI, Claude, Gemini and DeepSeek using the existing single YAML
configuration. The Claude direct-caller protocol fix passed 88 native tests;
saved-provider UI guidance passed 173 extension and 33 browser tests. The new
package passed ZIP integrity and actual VS Code opened-folder binding and was
installed in the normal profile, which needs a window reload. The older browser
preview and broad model eligibility remain pending.
[Current scope and evidence](native-provider-workspace-acceptance.md).

VS Code opened-folder checkpoint: the packaged extension passed actual host API
and authenticated Native identity checks on `D:\CantorAI2026\TestProj`, with no
`.code-workspace` file or manually entered server token. All 88 native contracts
passed after correcting Windows long lock paths; 171 extension and 32 browser
contracts also passed. The normal persistent window was installed and opened
with its trust settings unchanged and awaits the user's folder-trust choice.
[Scope and evidence](vscode-opened-workspace.md). No new screenshot or live model
inference is claimed for this checkpoint.

Current schema-v12 context, YAML import and DeepSeek source passed a complete
local gate. The first full 86-contract attempt passed **81 and failed five**;
the second full 88-contract attempt passed **85 and failed three in 162.98
seconds**, after configuration and compilation passed. After repository,
context-controller and CLI route fixture repairs, the third full gate passed
**88/88 in 161.48 seconds**, with zero failures/skips, 560 frozen inputs unchanged
and original test output captured. The failed attempts are retained without
exclusions. DeepSeek's synthetic gateway/profile contracts passed
individually in **0.54 / 0.71 seconds**; **131 extension / 29 browser** source
tests remain separate. The later Responses legacy-history serializer repair
passed the fourth complete **88/88 gate in 171.89 seconds**, with all 560 inputs
unchanged and no failures/skips. The final saved-provider footer and catalogue
retention changes passed **141 extension and 32 browser tests**, with all 37
frontend/vendor inputs unchanged. The later model-free native/browser
integration passed in **3.30 seconds** and the actual VSIX passed **18 asset
checks**, with all 12 tested view files matching and all 30 payloads hashed.
[Integration and package evidence](evidence/native-provider-footer-integration-provenance.json)
retains the initial guard-host failure. Hosted validation, installation and
live provider acceptance remain separate.
[Source-bound native gate](evidence/native-context-provider-local-provenance.json),
[Current context scope](native-context-compaction-design.md),
[provider scope](provider-setup.md).

The installed preview remains ace/schema v10 with one OpenAI Responses profile
and four routes; it has not imported the local four-key configuration and does
not advertise DeepSeek. An earlier request failed with HTTP 400 `invalid_value`;
an isolated direct reproduction later identified assistant `input_text` instead
of `output_text` in ace and current source. A fresh native OpenAI browser prompt
separately returned `OK` with supplied usage **3884 / 5**, without exercising
legacy replay or compaction. Direct account catalogue and small generation
checks passed for all four providers; native four-provider coding, installed
new-source clients and live compaction acceptance remain separate.

The latest accepted transport-only source `859aca7743e561e46fd574c82dff65df333a7bf4`
passed hosted **78 native contracts in 246.07 seconds**, **119 extension tests in
3.4198188 seconds** and **26 browser tests in 2.0534379 seconds**, all 16 steps,
model-free browser/native integration and 18 VSIX assets. Its immutable source,
archive payloads and original logs were verified. This predates the current
context/YAML/DeepSeek implementation and establishes no compaction or new preview
installation. [Exact hosted transport evidence](evidence/native-context-transport-hosted-859-provenance.json).

Exact source `3fc420480f61db75d3efaf4bda77fbefd8246166` subsequently passed its [hosted gate](https://github.com/xlang-foundation/xMind/actions/runs/37808089507): **77 native contracts in 206.89 seconds**, **119 extension tests in 2.4612639 seconds** and **26 browser tests in 1.3989153 seconds**. All 16 job steps passed, with the exact complete native manifest, zero failures/skips, model-free browser/native integration and 18 verified VSIX assets. Original logs, advertised archive digests and exact source/runtime maps were verified. Provider replies remain synthetic; this establishes the source checkpoint rather than installed or live planning acceptance. The installed preview remains ace/schema v10. [Artifact-bound hosted evidence](evidence/native-dynamic-plan-hosted-provenance.json).

Current completion scope is the [revised xMind OSS specification](architecture.md). Team-server features, PostgreSQL, WebRTC and the standalone Electron IDE are excluded and reserved for Nexus. Historical checkpoint paragraphs below preserve their original source/time scope and do not reinstate those requirements.

## Current checkpoint scope

The initial native dependency-planning checkpoint (schema v11) passed the complete guarded local **77-contract gate in 153.88 seconds**, with the exact expected/registered/passed manifest and all **421 frozen native source hashes** verified. Eligible ordinary Agents select agent/human dependencies, revise undispatched work and retain the same owner through clean human pause/reopen. Actual coding-child edits require separate exact approvals and can feed dependent verification. **119 extension and 26 browser contracts** passed at their source renderer/controller scope. Provider replies/signatures/keys are synthetic; native engines, files, permissions and embedded-xlang3 SQLite are real. The first **68/77** and second **76/77** attempts remain recorded failures. [Initial planning scope](native-dynamic-plan.md), [local source and gate evidence](evidence/native-dynamic-plan-local-provenance.json).

Model-free browser/native graph/human/auth/cookie/reconnect integration passed against the rebuilt server and matching assets; all **18 required VSIX assets** were packaged and verified. These checks establish neither rendered dynamic-planning nor installed-editor acceptance. The installed **ace2460/schema-v10** preview and its live read-only join remain separate. Schema-v11 installation, live planning and installed VS Code acceptance remain pending. Dynamic deterministic tool/MCP/process nodes, recursive planning, skills, compaction, outbound A2A and full coding/provider parity remain incomplete; private Nexus features stay outside OSS.

Native Responses reasoning continuation passed the exact ace2460 hosted gate: **71 native contracts in 171.52 seconds, 103 extension and 20 browser contracts**, with all 16 job steps, native/browser integration and 18 VSIX assets passing. Verified sources and artifact maps bind the completed-item continuation fix and its real native two-child read/join with synthetic provider replies. The local build was deferred by the benchmark guard. The exact native bundle is installed. One real OpenAI browser prompt completed two read-only native children and a joined parent response, with six owned model attempts, separate per-response metrics and zero effect operations. Immediate refresh retained the connection and selection. Upgrade and rollback checks passed at their separately documented scopes. [Compatibility and scope](native-responses-reasoning.md), [exact hosted evidence](evidence/native-responses-reasoning-hosted-provenance.json).

Ordinary Agent mode now offers native `delegate_tasks` for model-selected read-only workspace investigations. Actual child AgentRunners keep separate conversations and measured response usage/timing, share their parent's call/deadline limits, and return observed results before the parent continues or requests an approval-controlled effect. Schema v10 durably binds tasks, immutable presets, budgets and once-only settlements; recovery never replays children. CLI, browser and VS Code observe the same owned child histories and committed tree cursors.

For the preceding delegation checkpoint, the final guarded local **71-contract native gate passed in 142.36 seconds**, with an exact expected/registered/passed manifest, **45 frozen source hashes**, zero failures/skips and no exclusions. The delegation engine contract took **1.96 seconds**, actual HTTP/CLI/shared-controller acceptance **1.03 seconds**. **103 extension tests** and **19 browser tests**, fresh source-matched browser/native integration and 18-asset VSIX verification also passed. Provider replies, signatures and keys are explicitly synthetic; native agents, filesystem effects, permissions, xlang3 SQLite and transport are real. The [initial process-fixture positional-schema failure](evidence/native-delegation-initial-ctest.log) is retained separately; the corrected frozen source passed all 71 contracts.

Exact revision `e353a37799530a234a6fa13e51f61a5c52d3ae6a` subsequently passed its [hosted gate](https://github.com/xlang-foundation/xMind/actions/runs/37765666399): **71 native contracts in 175.51 seconds**, **103 extension tests in 2.9668103 seconds** and **19 browser tests in 1.426115 seconds**, plus native/browser integration and verification of all 18 required VSIX assets. The delegation engine took **4.88 seconds** and its HTTP/CLI contract **1.22 seconds**. All 16 job steps succeeded, with zero failures/skips and the exact complete native manifest. Downloaded artifact digests, exact source and runtime/stdlib pins were verified. [Exact hosted71 provenance](evidence/native-delegation-hosted-provenance.json), [raw hosted CTest](evidence/native-delegation-hosted-ctest.log), [original hosted job](evidence/native-delegation-passing-ci-job.log).

The preceding managed backend upgrade installed the exact hosted e353 bundle with schema v10; the later 7fe upgrade is recorded in the diagnostic scope. The actual upgrade preserved existing records, settings and browser access; its disposable schema 9→10→9 rollback fixture passed separately. The original e353 webpage exposed a classic-script name collision. A separate local browser repair passed **103 extension tests in 1.6557025 seconds**, **20 browser tests in 0.9090882 seconds**, native/browser integration and all 18 VSIX assets. The repaired page reused its cookie without key entry and restored prior history/metrics, the selected model and Agent mode. These frontend results are separate from the unchanged hosted 71/103/19 gate. [Upgrade evidence](evidence/native-delegation-upgrade-provenance.json), [acceptance scopes](native-delegation-acceptance.md).

The first e353 live `delegate_tasks` request failed with `responses_terminal_mismatch` before child admission: zero children, only the user history row and one parent model-budget attempt. That historical failure remains recorded; the later ace browser two-child join passed its separate acceptance above. Installed VS Code delegation acceptance remains unverified. The initial v11 planning source checkpoint is distinct from those installed/live results; broader planning, skills, compaction, outbound A2A and full provider/coding parity remain incomplete. [Implementation and limits](native-delegation.md), [broader native planning design](native-dynamic-plan-design.md).

The preceding direct MCP graph checkpoint adds model-free registered tool nodes
using the real native schema, controller approval and operation journal. Each
node pins a server revision and registry alias; fresh discovery must match before
proposal or dispatch. Exact raw literals survive inside `arguments_json`
strings, while inexact typed dependency numbers reject before child admission.
Root deadlines bound discovery/waits/dispatch. Actual acknowledged oversized
outputs retain succeeded operations; uncertain or unrecorded effects preserve
quarantine/fail-stop ownership without replay. Metadata/admission never start
peers, and stale human pauses remain inspectable/cancellable with resume rejected
before input commit. The stopped-backend native administrator supports private
credential discovery and catalogue reflection rejection.
[Direct MCP graph scope](native-graph-mcp.md).

The final complete local **69-contract native gate passed in 121.22 seconds**,
with the exact expected/registered/passed manifest, zero failures/skips and no
post-build exclusions. All **14 frozen source hashes** match the final pre-build
state. The new native graph/permission contract passed in **5.59 seconds**;
actual administrator/server/CLI/shared-view-adapter integration in **2.31
seconds**. Owned peers perform real approved file effects, with dependent native
reads, human pause/reopen, denied/cancelled/invalid/drift/stale paths, serialized
shared-server claims, lost replies, acknowledged output limits and injected
journal fail-stop/recovery. Peer metadata/protocol replies are synthetic. Fresh
browser/native integration passed the rebuilt server and source-matched assets.
Its first HTTP launch failure exposed an obsolete model-only guard; the [failed
interim log](evidence/native-graph-mcp-initial-ctest.log) is retained separately
from the final passing corrected-source gate and additional invalid model
configuration cases. Exact revision
`c4ec09fc3ee25c1b3a2bd9087b25f5af420ee616` subsequently passed its
[hosted gate](https://github.com/xlang-foundation/xMind/actions/runs/37755382937):
**69 native contracts in 184.36 seconds**, **98 extension tests in 3.2107402
seconds**, **17 browser tests in 1.5543002 seconds**, native/browser integration
and 18-asset VSIX verification. All 16 job steps passed, with an exact native
manifest and zero failures/skips. Live direct-MCP graph, rendered IDE and
complete parity acceptance remain incomplete.
[Exact local69 evidence](evidence/native-graph-mcp-local-provenance.json),
[exact hosted69 evidence](evidence/native-graph-mcp-hosted-provenance.json).

The [delegation design](native-delegation-design.md) now has the initial local/hosted-verified
implementation and transactional v10 migration described above. Broader dynamic
plans and live/client release acceptance remain required.

The preceding `fceb50b` Claude receipt checkpoint passed the full local **67-contract native
gate in 117.65 seconds**, with an exact manifest, zero failures/skips and no
post-build exclusions. The new history helper passed in **0.50 seconds**; the
expanded actual AgentRunner contract passed in **0.57 seconds**. Ordered
text/tool/thinking/redacted blocks, exact initial/fragmented tool-input JSON,
precise number tokens and escaped keys survive validated receipt persistence
and raw outgoing Messages replay. Two real file reads execute through
interleaved signature-bearing blocks; encrypted credentials and ordered history
survive xlang3 SQLite close/reopen without repeating tools. Unsigned blocks fail
before effects, while previous protocol failure, cancellation/recovery and SQL
conversation rollback cases still pass.

Hidden thinking/signatures/redacted data remain continuation material rather
than ordinary text output. Native matching/provenance checks and request,
receipt, message-count, depth and aggregate/role-merge bounds passed: 8 MiB
content/envelope and final request limits, 64 content blocks, 1 MiB/depth16 tool
input and 65,536-byte signatures. Fresh browser/native integration passed the
rebuilt server and matching assets. Frontend source equality with `ae7c4ba`
retains its prior **98 extension and 17 browser tests**, without a new frontend
rerun. Provider replies/keys/signatures are synthetic; local cryptographic
verification is not performed. Thinking/reasoning request controls, live Claude
acceptance, foreign signed/tool conversion, rendered IDE acceptance, preview
upgrades and complete parity remain incomplete. Exact revision
`fceb50ba0493f645ee5f0a00d5400e0f103df231` subsequently passed its
[hosted gate](https://github.com/xlang-foundation/xMind/actions/runs/37752689263):
**67 native contracts in 120.18 seconds**, **98 extension tests in 2.0700124
seconds** and **17 browser tests in 0.9230928 seconds**, native/browser integration
and verification of an 18-asset VSIX, with zero failures/skips and the exact
manifest. Hosted history/agent contracts took **0.31/1.03 seconds**. The hosted67
gate excludes the current direct MCP graph, administrator and server startup
changes.
[Local67 evidence](evidence/native-anthropic-history-local-provenance.json),
[exact hosted67 evidence](evidence/native-anthropic-history-hosted-provenance.json),
[receipt scope and bounds](native-claude-history.md).

The preceding `ae7c4ba` Claude checkpoint passed the full local **66-contract native gate
in 114.49 seconds**, with an exact manifest, zero failures/skips and no post-build
exclusions. Its new actual AgentRunner contract took **0.56 seconds**, verifying
two native file reads, correlated tool-use/results, encrypted credential reuse,
xlang3 SQLite history/reopen without repeated tools, held-stream cancellation
and recovery, and second-tool-row SQL rollback. Its normalized usage adapter
contract passed in **0.23 seconds**. Supplied uncached input/output/cache counters
and zeros remain distinct in events and persisted responses, without sums or
invented totals.

Frontend suites passed **98 extension and 17 browser tests**, with zero
failures/skips and actual shared-renderer DOM metric fixtures. Fresh browser/native
integration passed against that rebuilt server and matching assets. Exact
revision `ae7c4ba07d59f57cbe393eff5a01b7d5186dfbc5` subsequently passed its
[hosted gate](https://github.com/xlang-foundation/xMind/actions/runs/37748603655):
**66 native contracts in 131.92 seconds**, **98 extension and 17 browser tests**,
native/browser integration and VSIX verification, with zero failures/skips and
an exact manifest. Its Claude agent contract passed in **1.41 seconds**.
Both prior gates exclude the newer ordered signed/redacted receipt work above;
this is separate from hosted67 verification, CLI65 and hosted enrollment64.
Live Claude inference, rendered Claude IDE acceptance, the 21 pinned models and
full parity remain incomplete. Installed previews are unchanged.
[Local66 source/runtime/log evidence](evidence/native-anthropic-agent-local-provenance.json),
[exact hosted66 evidence](evidence/native-anthropic-agent-hosted-provenance.json),
[Claude scope](native-claude-agent.md).

The user-requested native-only cleanup was committed and pushed as
`dea588874e5a8c8e40bf1a158fa925520443c8a0`. The Python agent prototype and
old xlang Core/service/plugin assets are removed, root CMake delegates to
`Native/`, and maintained documentation is merged into `doc/`. Its hosted gate
passed **58 native, 88 extension and 17 browser contracts**, native/browser
integration and VSIX verification, with no failures/skips.
[Exact cleanup evidence](evidence/native-cleanup-hosted-provenance.json).

Gemini transport/replay source
`ddd1d3da087f9ca7f8b0b8d81705c82632e15094` passed a separate hosted gate:
**60 native, 88 extension and 17 browser contracts**, native/browser integration
and VSIX verification, with no failures/skips. This verifies native request,
SSE/HTTP transport and signed replay DTOs with synthetic socket fixtures.
[Exact transport evidence](evidence/native-gemini-transport-hosted-provenance.json).

The common gateway/history source
`75f45f0a036f1ffbab8c4b157df364f3697b52a0` passed its exact hosted gate:
**61 native, 91 extension and 17 browser contracts**, native/browser integration
and VSIX verification, with zero failures/skips. Its native gate took **121.84
seconds** and includes the stronger callbacks and escaped large-response cases
excluded from the earlier historical local 61 result.
[Exact hosted evidence](evidence/native-gemini-history-hosted-provenance.json).

The agent source `c9591fe79cad9a4253ac8088933f0c8a2848ded1` passed **62 native
contracts locally in 100.44 seconds**. Its real native agent/file reads,
xlang3 SQLite reopen and signed replay do not repeat prior tools. Provider
replies are synthetic. Browser/native integration passed again against its
compiled server, and its unchanged thin clients matched the earlier 91/17 pass.
[Local agent evidence](evidence/native-gemini-agent-local-provenance.json).
The exact hosted gate also passed **62 native, 91 extension and 17 browser
contracts**, native/browser integration and VSIX verification, with zero
failures/skips. Its native gate took **128.80 seconds** and excludes newer
catalogue/enrollment source.
[Exact hosted agent evidence](evidence/native-gemini-agent-hosted-provenance.json).

Enrollment source `2f5e0f05e9bfadcf86b5508863da0ec5a9e78cfe` passed its
exact hosted **64 native, 94 extension and 17 browser contracts**, native/browser
integration and VSIX verification, with zero failures/skips. The native gate
took **158.09 seconds**, following the local 64-contract pass in **120.98
seconds**. Authenticated Gemini pagination/full resources, encrypted enrollment,
owned-key file execution, cancellation/CAS failure, stale discovery and signed
xlang3 SQLite replay passed with synthetic replies, keeping generation methods
separate from backend tool policy. Direct-SQLite removal and shared `records.hpp`
cleanup are included.
[Exact hosted enrollment evidence](evidence/native-gemini-enrollment-hosted-provenance.json).

The preceding CLI checkpoint passed all **65 native contracts locally in 113.16
seconds**, with the exact manifest, zero failures/skips and no post-build
exclusions. Its new CLI contract took **4.23 seconds** and covers OpenAI, Claude
and Gemini profile catalogue/setup/selection, private environment input,
all-family public identity reflection rejection, safe provider diagnostics,
signed Gemini text/history/usage and actual SQLite reopen with encrypted credentials. Stale
discovery/admission does not retry or silently rebind; failed-turn status survives
settings success and recovers only through a later actual successful turn.
Fresh browser/native integration passed again against the rebuilt server and
source-matched assets. That checkpoint's unchanged frontend sources retained the earlier verified
**94 extension and 17 browser tests**, without rerunning those suites for the
CLI change. [Local CLI evidence](evidence/native-provider-profile-cli-local-provenance.json).
Exact revision `ac69c1f2a2c4a757b23d0d9da8b98d6188fab3a8` subsequently passed
its [hosted gate](https://github.com/xlang-foundation/xMind/actions/runs/37746664258):
**65 native contracts in 149.37 seconds**, **94 extension and 17 browser tests**,
native/browser integration and VSIX verification, with zero failures/skips and
the exact expected manifest.
[Hosted CLI evidence](evidence/native-provider-profile-cli-hosted-provenance.json).
That hosted result excludes the newer Claude agent/metrics and ordered-receipt
milestones. Installed
previews are unchanged; live Gemini inference, actual Gemini IDE acceptance and
full provider/coding/protocol parity remain incomplete.
[Provider setup](provider-setup.md), [CLI scope](native-interactive-cli.md).

The preceding `c4ec09fc` installed browser preview matched all 29/12 bundle/view hashes. Prior records and
settings were preserved; an actual read-only OpenAI Agent request, visible
metrics and subsequent refresh passed. Its original browser session needed one
reconnect; the exact cause remains unresolved. The disposable actual upgrade
and explicit rollback passed separately before installation.
[Historical installed acceptance](preview-checkpoint-c4ec09fc.md).
The preceding e353 backend upgrade, separate browser repair and first failed live
delegation request are recorded in [current acceptance](native-delegation-acceptance.md)
and [current validation](VALIDATION_STATUS.md).

All counts describe bounded verification, not product or OpenCode parity
percentages. Broad native providers, full coding workflows, remaining MCP/A2A,
graph behavior and Local/Nexus connection contracts remain active work.

## Historical native milestone records

The records below retain their original revision/time and fixture scope. Words
such as "current", "now", "pending" and installed-preview descriptions inside
these records apply to that historical checkpoint; the summary above is the
current source/verification status. No removed prototype is a supported runtime
or delivery requirement.

Native Responses provider checkpoint: explicit C++ wire selection now reaches the shared single-agent and graph engine, preserving stateless reasoning/tool continuation, actual supplied token metrics and persisted conversation replay. Two dependent agents perform actual file reads; provider-native items stay in child conversations rather than graph dependency/join outputs. Final local Release validation passed **52 native and 60 extension contracts**, no skips. [Scope and evidence](native-responses-provider.md). Inference/opaque reasoning are synthetic; live provider/IDE acceptance, interactive wire enrollment, standalone browser UI, broader provider coverage and full product parity remain pending.

The earlier native A2A v1 source `d07c61c725c3a1a4bf9ac85a2c6782575e52d8df` passed hosted **50 native and 60 extension contracts**, no skips, in [run 37693026697](https://github.com/xlang-foundation/xMind/actions/runs/37693026697). [Original full hosted job](evidence/native-a2a-v1-hosted-job.log). This result applies to that source, not the newer history/Responses changes or installed UI runtimes.

Native A2A history fidelity: protocol histories now preserve the admitted text-part boundaries, empty parts, and message/part metadata across both wire versions, completed stream replay and restart, while keeping model prompts separate and task histories isolated. Final local verification passed **50 native and 60 extension contracts**. The older flattening assertion's failure and its replacement checks are documented in [scope and evidence](native-a2a-v1.md). Hosted validation and preview installation of this source remain pending; full protocol/product parity remains incomplete.

Native A2A 1.0 core RPC checkpoint: direct versioned discovery/send/get/list/cancel/stream/subscribe now use the shared C++ executor and xlang3 repository, with default blocking semantics, cross-version durable retries and measured status times/cursor listing. The official SDK **1.3.0** exercised the native v1 wire without compatibility translation. Final local validation passed **50 native and 60 extension contracts**. [Scope, migration behavior and evidence](native-a2a-v1.md). Full protocol/product parity remains incomplete; hosted verification of this source and preview installation are pending.

Earlier native A2A discovery/streaming: authenticated discovery, Task-first SSE, persisted partial text/refusal, reconnect without reexecution and bounded viewer capacity now use the shared C++ executor and xlang3 journal. An official SDK **1.3.0** peer passed explicit v0.3 compatibility discovery/stream/get checks. The final isolated Release build passed **48 native and 60 extension contracts**. The hosted graph pause/shutdown failure is preserved and its ownership race is fixed with repeated regression cases. [Scope and evidence](native-a2a-streaming.md). Hosted verification at `b13ea224b026b9b750f43bdd311254ca7855b53c` passed all **48 native and 60 extension contracts** in [run 37689748037](https://github.com/xlang-foundation/xMind/actions/runs/37689748037). The newer checkpoint above adds native v1 and blocking; continuation, remote delegation and full platform parity remain incomplete. Previews retain their current runtimes.

Earlier native A2A message admission: nonblocking text requests now use real C++ agent admission and xlang3 persistence, with atomic durable retry identities and task-local histories. The isolated Release build passed **47 native and 60 extension contracts**, including rollback/migration and actual server/worker transport with explicitly synthetic provider replies. [Scope and evidence](native-a2a-message-admission.md). At that checkpoint discovery and streaming were incomplete; the newer milestone above adds these components. Remote delegation and full platform parity remain incomplete.

Earlier native A2A task-control component: the C++ `/a2a` adapter reads actual shared tasks/artifacts and requests owner-controlled cancellation, preserving accurate observed states and rejecting internal child IDs. Final local Release validation passed **45 native and 60 extension contracts**, including actual graph/file/SQLite task execution and repeated finish/cancel scheduling. [Scope and evidence](native-a2a-task-control.md). At that checkpoint, admission and task-local history were incomplete. The newer milestone above adds those components; the subsequent streaming checkpoint adds discovery and scoped SDK interoperability. Remote/full protocol interoperability remains incomplete. This is not full A2A compliance or an installed-preview claim.

Current graph UI checkpoint: the actual right sidebar submitted `read.repository.file`, paused for human input, read the real repository `README.md`, and displayed matching bytes. Its completed root and transcript survived an editor reload. The isolated `graph-ui` backend/profile preserves the user's existing model preview and open Settings dialog. Join output is compact and expandable, and workflow choice persists per backend. **60 local extension contracts** pass. [Actual IDE scope and evidence](vscode-graph-workflows.md). Agent graphs with live inference/metrics and the remaining full product scope are still incomplete.

The native graph ownership/HTTP/CLI checkpoint passed 44 native contracts locally, followed by seven final access regressions; the sidebar's production client passed against compiled C++/xlang3 persistence. [Backend commands and scope](native-graph-service.md). The newer hosted builds must be reported from their exact GitHub run outcomes; these local checks do not establish hosted success. Earlier milestone paragraphs below retain their original source/time scope.

Automatic account model discovery: installed backend `5815141dcf27652a1af503e39f17127ba189c403` passed **43 native and 47 extension contracts** in [isolated CI](https://github.com/xlang-foundation/xMind/actions/runs/37665810113). [CTest](evidence/native-model-discovery-saved-key-hosted-ctest.log), [original job/TAP](evidence/native-model-discovery-saved-key-passing-ci-job.log), [provenance](evidence/native-model-discovery-saved-key-hosted-provenance.json). Manual ID input is replaced by actual API discovery and a searchable picker. Encrypted saved keys can be reused without another password, and legacy credential-as-model records no longer advertise a key or enable inference. The native preview was upgraded with its database/profile/draft preserved. Its actual account discovery was rejected by OpenAI for authentication/authorization; the actual private key replacement prompt is open. The adapter-only replacement action passed **48 local extension contracts**, separately from the hosted backend checkpoint. [Launch/local adapter verification](evidence/vscode-model-discovery-launch.json), [scope](provider-setup.md). Successful account listing and live provider inference remain pending.

Provider setup installed: source `86ff065cafeb0b43f9bc47adb2d16b631dacade3` passed **42 native and 44 extension contracts** in [isolated CI](https://github.com/xlang-foundation/xMind/actions/runs/37657184396). [Hosted CTest](evidence/native-provider-setup-hosted-ctest.log), [original job/TAP](evidence/native-provider-setup-passing-ci-job.log), [provenance](evidence/native-provider-setup-hosted-provenance.json). The actual preview now runs this bundle and opened its native model setup prompt, preserving the database/profile and user's draft. [Launch evidence](evidence/vscode-provider-setup-launch.json), [scope](provider-setup.md). The native contract's provider replies/key are synthetic; entering the user's key and receiving an actual provider response remain pending. Subsequent CLI enrollment source is still under verification.

Graph executor source `77ffa610661abdab910b270e086c1bedf9f2a325` now passed **41 native and 41 extension contracts** in [isolated CI](https://github.com/xlang-foundation/xMind/actions/runs/37655834894). [Hosted CTest](evidence/native-graph-runner-hosted-ctest.log), [original job/TAP](evidence/native-graph-runner-passing-ci-job.log), [provenance](evidence/native-graph-runner-hosted-provenance.json). This proves the native component contracts with synthetic inference; public graph service/client workflows remain pending. The subsequent provider-setup source is still under exact-revision native verification, and the installed preview remains model-free.

Provider setup source checkpoint: native authenticated model/key enrollment, encrypted xlang3-backed credential references, idle-service replacement and persisted reopen are prepared with a bottom-composer setup control. All **44 extension contracts pass locally**, including private password/dismissal/late-grant fixtures. [Adapter output](evidence/vscode-provider-setup-local.log), [implementation and verification limits](provider-setup.md). Native compilation/integration remain pending behind the observed local xlang3 benchmark guard and isolated CI. The current preview has not been upgraded; no live chat or provider acceptance is claimed.

Native graph executor component: schema-v7 immutable task input, bounded parallel agent/tool dispatch, typed dependencies, conditional skips and durable human pause/resume now use actual backend workers. Release compilation and all **41 native contracts passed locally**. The graph contract exercises real file reads/creation, separate approvals, cancellation and injected outcome-journal failure/recovery without replay; provider replies remain synthetic. [Complete local build/CTest output](evidence/native-graph-runner-local-build-ctest.log), [source/binary provenance](evidence/native-graph-runner-local-provenance.json), [component and remaining scope](native-graph-runner.md). Public graph service/HTTP/CLI/view workflows and live-provider acceptance remain pending. The local sidebar launcher now accepts explicit model/endpoint/workspace settings and passes the provider key only to the native encrypted credential store; sidebar provider setup and a live response remain unverified while the user's key location is pending. All **41 extension contracts pass locally**: [output](evidence/vscode-model-startup-local.log).

Earlier durable checkpoint source `b9e96fceb8a8cc25db597f51b2a50583294b6009` passed **40 native and 41 extension contracts** in [isolated CI](https://github.com/xlang-foundation/xMind/actions/runs/37653997518). [Hosted CTest](evidence/native-graph-checkpoint-hosted-ctest.log), [original job/TAP](evidence/native-graph-checkpoint-passing-ci-job.log), [provenance](evidence/native-graph-checkpoint-hosted-provenance.json). The installed model-free preview remains on its earlier verified foreground-process bundle.

Durable graph coordinator checkpoint: schema-v6 admission, observed-child settlement, human wait/input and conditional skips commit their checkpoint/event updates together. All **40 native contracts passed locally**, including actual SQLite revision conflicts, event-fault rollback and pause/reopen/resume. [Complete output](evidence/native-graph-checkpoint-local-build-ctest.log), [provenance](evidence/native-graph-checkpoint-local-provenance.json), [scope](native-graph-checkpoints.md). Expanded hosted CI and public graph scheduling/client acceptance remain pending.

Earlier child-execution source `fbf2cfe9962c6d364893090e4590f765f92b61a3` passed **39 native and 41 extension contracts** in [isolated CI](https://github.com/xlang-foundation/xMind/actions/runs/37651959372). [Hosted CTest](evidence/native-graph-children-hosted-ctest.log), [original job/TAP](evidence/native-graph-children-passing-ci-job.log), [provenance](evidence/native-graph-children-hosted-provenance.json). Inference is synthetic; installed preview remains on its earlier verified bundle.

Native graph child checkpoint: schema-v5 parent/child records and private conversations pass all **39 native contracts locally**, including concurrent shared-engine child requests, actual file reads, atomic admission rollback, root retirement guards, identified event replay and reopen/recovery. Inference is synthetic; public graph scheduling and client workflows remain pending. [Complete output](evidence/native-graph-children-local-build-ctest.log), [provenance](evidence/native-graph-children-local-provenance.json), [boundaries](native-graph-children.md). Expanded hosted CI is pending.

Native graph foundation checkpoint: Release compilation and all **38 native contracts passed locally**, including dependency joins, typed conditions/references, human-wait checkpoint restore, interrupted-node uncertainty/non-replay, actual xlang3/SQLite catalog revisions/faults/reopen and authenticated native admin/server ownership. Coordinator outputs are synthetic; actual agent/tool graph execution and client workflows remain pending. [Complete output](evidence/native-graph-foundation-local-build-ctest.log), [provenance](evidence/native-graph-foundation-local-provenance.json), [component and integration contract](native-graph-foundation.md). Expanded hosted CI is pending.

Native repository guidance approval checkpoint: Release compilation and all **37 native and 41 extension contracts passed locally**. Durable edit/create/command proposals bind observed guidance sources; after approval, C++ rechecks before dispatch and retires mismatches without an effect. Actual changed/added/removed source contracts require a separate second approval, while sidebar contracts show recorded source hashes and reject malformed review data. [Native output](evidence/native-guidance-approval-local-build-ctest.log), [extension output](evidence/vscode-guidance-approval-local.log), [provenance](evidence/native-guidance-approval-local-provenance.json), [boundaries](native-repository-instructions.md). Expanded hosted CI and live/populated view acceptance remain pending.

Earlier repository discovery source `5122d6a1842ea92c0704fe297386c3a80622b881` passed **37 native and 40 extension contracts** in [isolated CI](https://github.com/xlang-foundation/xMind/actions/runs/37644164701). [Hosted CTest](evidence/native-repository-instructions-hosted-ctest.log), [original full job/TAP](evidence/native-repository-instructions-passing-ci-job.log), [hosted provenance](evidence/native-repository-instructions-hosted-provenance.json). Inference is synthetic; the installed backend preview remains on its earlier foreground-process bundle.

Native instruction configuration checkpoint: source `61a5d692d7d205df0d7f5a630f307c529e14dc15` passed **36 native and 40 extension contracts** in [isolated CI](https://github.com/xlang-foundation/xMind/actions/runs/37642796445). Actual xlang3/SQLite revisions/faults/reopen, admin/server/model/CLI delivery, immutable owner lease and continued native permissions are verified with synthetic inference. [Hosted CTest](evidence/native-agent-instructions-hosted-ctest.log), [original full job/TAP output](evidence/native-agent-instructions-passing-ci-job.log), [provenance](evidence/native-agent-instructions-hosted-provenance.json), [component limits](native-agent-instructions.md). The installed preview remains on the earlier foreground-process bundle.

Verified native foreground process checkpoint: source `3297a4bce2d591c0f1c1dd37ccc18a15297369e7` passed **35 native and 40 extension contracts** in [isolated CI](https://github.com/xlang-foundation/xMind/actions/runs/37635775031). Actual child/file effects cover descendant timeout/cancellation, executable binding, approved/denied/stale commands, bounded binary output, SQLite journal failures/reopen, retained-output cursor replay and native CLI watcher disconnect/reconnect. Inference is synthetic. [CTest](evidence/native-process-passing-ci-ctest.log), [full job/TAP log](evidence/native-process-passing-ci-job.log), [provenance](evidence/native-process-passing-ci-provenance.json). The actual [unseeded right-sidebar preview](evidence/vscode-native-process-bundle-sidebar.png) is running the tested bundle with its database/profile preserved. No live-model/populated coding claim follows; full shell/background/PTY, protocol/graph/provider/team/Electron scope remains required. Earlier source-prepared and failed records below remain historical.

Durable foreground output source checkpoint: the native executor journals a bounded byte-exact output prefix through embedded xlang3; the sidebar reconstructs independent stdout/stderr with explicit missing-sequence/binary handling. All **39 extension contracts passed locally** with labelled fixtures: [complete output](evidence/vscode-process-stream.log). Actual pre-exit output, SQLite replay/write failures and HTTP/CLI restart contracts are expanded in source and await exact-revision CI. The parent-exit cleanup correction remains included. There is no passing expanded process bundle or populated/live-provider editor acceptance yet. [Implementation and limits](native-process-tools.md).

The user requested visible milestones. Show a runnable result, validation evidence and remaining scope at each milestone. Prepared source or a diagram alone does not prove a runnable native milestone.

Next coding component: the native foreground process adapter, durable approval executor, executable binding, persisted profile administration and model/server/CLI source are prepared, with 35 native contracts expected by isolated CI. Local compilation was deferred by a currently observed xlang3 timing controller; no process pass or verified model/server/editor integration is claimed. [Implementation/acceptance contract](native-process-tools.md). The latest verified runnable product remains the 31-native/30-extension creation checkpoint below.

Sidebar command review/output contracts now pass all **34 local extension tests**, including literal argument review, escaped/control/hex output, actual supplied exit/timing/count fields, malformed/expired proposal rejection and uncertain-command handling: [complete output](evidence/vscode-process-review.log). These are labeled DOM/host fixtures, not a populated command run in the interactive preview. Native process profile configuration and end-to-end model/server/editor validation remain required.

## M1: persistence through embedded xlang3 — verified

Verified supporting component: native Windows credential protection compiled and passed its synthetic binary/context/tamper/ownership test. It is now connected to SQLite through embedded xlang3; see [credential-storage.md](credential-storage.md). The historical native test log `build/native/evidence/contracts-20261006.log` preserves the passing protection test and the two initial failing SQLite tests; the later passing logs below supersede those failures.

Release compilation and native seed/read demo passed on October 6, 2026 after the user-authorized SQLite module fixes on `agentflow/mcp-native-compat`. Seed persisted a session, two messages, agent configuration and four ordered events. Independent native read processes reopened that state; `-After 2` returned only events 3 and 4. All three default native contracts passed ten consecutive runs. Evidence: `build/native/evidence/contracts-after-sqlite-fix-20261006.log` and `contracts-repeat-after-sqlite-fix-20261006.log`. Earlier failure evidence remains in `contracts-20261006.log`.

The xlang3 legacy SQLite fixtures and the new isolation/text fixture also passed. This milestone proves initial native persistence contracts, not the agent engine, HTTP service, remote storage or UI. Encrypted SQLite credentials passed the following repository checkpoint. The other checkout's 97-case benchmark process exited before the module build; recheck processes before subsequent heavy builds.

C++ demo and contracts are prepared. The source creates a session, two conversation messages, a completed run, durable events and a general agent-configuration record, using only xlang3 for SQLite operations. A second process reopens the database and reads history/events; a cursor limits replay. Existing sessions are not overwritten by seeding. Encrypted secrets are not implemented by this demo.

On an idle machine:

```powershell
.\Tools\native-milestone.ps1 -Action Build
.\Tools\native-milestone.ps1 -Action Seed
.\Tools\native-milestone.ps1 -Action Read
.\Tools\native-milestone.ps1 -Action Read -After 2
```

The build action refuses to run while observed xlang3 benchmark processes are live. Exit 3 means deferred, not passed. Build/configuration and contract failures propagate nonzero exit codes. The launcher never executes CPython. Build output and the demo database are separate from the installed WorkSense application and other xlang3 checkout.

Delivered evidence: Release build, passing default embedded adapter/repository contracts, seed/read in separate native processes and exact cursor replay. Affected-row assertions remain intact. Wider Python engine/A2A historical failures and graph closure behavior are separate outstanding investigations; the native milestone does not establish their resolution.

## Following milestones

Verified native creation/SDK checkpoint: [isolated run 37621404384](https://github.com/xlang-foundation/xMind/actions/runs/37621404384), exact source `e93f28c7e6d7c214f1c0d5e049ad9408128b4ad5`, passed Release compilation and all **31 native and 30 extension contracts**. Actual files establish approved creation including empty/Unicode content, no overwrite, recorded parent identity, directory-junction rejection, denial/cancellation before dispatch, real result continuation, pending-approval restart and actual creation followed by an outcome-storage fault recovered as uncertain without replay. Official modern/legacy MCP SDK peers and both native schema dialects also passed. [Complete CTest output](evidence/native-file-creation-ci-ctest.log), [provenance](evidence/native-file-creation-ci-provenance.json), [scope and remaining creation work](native-file-creation.md). Inference is synthetic; populated editor/live-provider execution, cancellation after physical creation, process death during partial writes and attributed creation reconciliation remain unverified. The actual unseeded sidebar now runs this tested development bundle with its preserved session/profile and no configured model: [screenshot](evidence/vscode-native-creation-bundle-sidebar.png), [launch metadata](evidence/vscode-native-creation-bundle-launch.json). The next coding component is the [native process executor](native-process-tools.md); its acceptance contract is documented and implementation remains pending. Earlier pending/source-only statements below are superseded by this exact result. The full project goal remains active.

Native creation implementation prepared, **not yet natively verified**: [source scope](native-file-creation.md). The agent/executor and opened-handle create-new path now have contracts for actual files, conflicts, parent identity, junction rejection, Unicode/empty content and outcome-storage fault/restart; model/HTTP/CLI creation cases are prepared too. All **30 extension contracts passed locally** for absence-aware review/comparison and uncertainty rendering: [original extension output](evidence/vscode-native-creation-review.log). Local native compilation deferred during the separate xlang3 full timing suite. The prior thirty-contract SDK [CI run](https://github.com/xlang-foundation/xMind/actions/runs/37619215608) failed before native configuration because two `npm.cmd` paths were combined; [actual error excerpt](evidence/native-mcp-sdk-ci-resolver-failure.log). Corrected source `e93f28c7e6d7c214f1c0d5e049ad9408128b4ad5` selects one application and schedules the complete proposed 31-contract [isolated run 37621404384](https://github.com/xlang-foundation/xMind/actions/runs/37621404384). New native behavior is pending, and the goal remains active.

Official SDK tool interoperability checkpoint: source `bbeb4db077adcb5cbb86c9e91d3d3fe199886e0f` compiled in Release and passed all **30 native contracts locally**, including actual modern SDK 2.3.1 and legacy SDK 1.32.1 stdio peers. The native agent/server/admin/CLI path verifies discovery/protocol selection, encrypted environment, exact incoming argument bytes, approved real file effects, denial/cancellation, result continuation and persisted lost-reply uncertainty without replay. Native schema validation now retains distinct Draft-07 and 2020-12 semantics inside the existing bounded worker; tests exercise legacy tuple/dependency rules and `$ref` sibling differences. [Original full build/CTest output](evidence/native-mcp-sdk-local-build-ctest.log), [source/runtime/worker and pinned SDK provenance](evidence/native-mcp-sdk-local-provenance.json), [fixture scope](../Native/tests/sdk/README.md). Inference is synthetic. Full SDK feature coverage, HTTP/OAuth/resources/prompts, modern interactive continuations, account-resource mapping, complete schema conformance and live/populated UI execution remain incomplete. [Isolated CI run 37619215608](https://github.com/xlang-foundation/xMind/actions/runs/37619215608) is in progress; the goal remains active.

The preceding configured-MCP revision `1a3d5c1d5542cc1ce8eb3db1162063f7e5425b9c` passed isolated [CI run 37617251064](https://github.com/xlang-foundation/xMind/actions/runs/37617251064), all **28 native and 28 extension contracts**: [CTest output](evidence/native-agent-mcp-ci-ctest.log), [provenance](evidence/native-agent-mcp-ci-provenance.json). It predates Draft-07 validation and official SDK peer contracts.

Checkout/preview checkpoint: source, Git history, editor profile and session database now live in `D:\CantorAI2026\xMind`. The actual normal development host reopened its existing session with Explorer left and xMind/composer/model chooser right, using the locally verified MCP integration binary and no model or seeded messages: [launch evidence](evidence/vscode-xmind-checkout-launch.json), [actual screenshot](evidence/vscode-xmind-checkout-sidebar.png). A fresh CMake configure/Release build in `xMind\build\native` passed all **28 native contracts** after relocation: [original complete output](evidence/native-xmind-root-build-ctest.log), [source/binary provenance](evidence/native-xmind-root-build-provenance.json). Native and extension implementation trees are unchanged from `1a3d5c1`; intervening commits update documentation/evidence. Only prior locked build artifacts remain under the old `AgentFlow\build` directory; the active checkout and fresh build use xMind.

Native configured MCP agent checkpoint: Release compilation and all **twenty-eight** native contracts passed locally at `1a3d5c1d5542cc1ce8eb3db1162063f7e5425b9c`. The real compiled administrator persists registered stdio configuration and command-bound DPAPI credential references through embedded xlang3. The actual server restores those settings, discovers subprocess tools per run, exposes them to the agent and routes exact proposals through controller approval and durable multi-resource claims. Independent MCP peers actually write fixture files; synthetic inference verifies continuation after peer acknowledgement, denial/cancellation and lost-reply uncertainty retained across restart without replay. All **twenty-eight extension contracts** passed, including external-tool approval labels and uncertainty rendering. [Original build/CTest evidence](evidence/native-agent-mcp-local-build-ctest.log), [source/binary provenance](evidence/native-agent-mcp-local-provenance.json), [renderer/extension evidence](evidence/vscode-mcp-renderer.log), [trusted setup](native-mcp.md). Actual populated editor MCP execution, live inference, SDK interoperability, Streamable HTTP/OAuth, account mapping and complete schema conformance remain incomplete. [Isolated CI run 37617251064](https://github.com/xlang-foundation/xMind/actions/runs/37617251064) is in progress; the goal remains active.

Native MCP resource/worker checkpoint: Release compilation and all **twenty-six** native contracts passed locally at `9582dee1e17f37fe5e00220dda4c1a63f53188fe`. Repository schema four atomically locks the opened workspace and stable configured-server resources; real subprocess effects verify cross-workspace quarantine, while repository fixtures verify contention, revision changes and migration/backfill/rollback. Schema admission and input/output validation use a bounded native child with real Windows memory/time/process budgets. Independent fixtures verify actual memory-allocation refusal, stalled-child timeout/cancellation and subsequent healthy production-worker validation. Worker failures, genuine schema rejection and peer tool errors remain distinct; claimed cancellation/result-storage failures retain fail-stop ownership. [Complete build/CTest evidence](evidence/native-mcp-resources-schema-local-build-ctest.log), [source/binary provenance](evidence/native-mcp-resources-schema-local-provenance.json). Its isolated [CI run 37614949089](https://github.com/xlang-foundation/xMind/actions/runs/37614949089) also passed, all twenty-six native and twenty-seven extension contracts: [CTest evidence](evidence/native-mcp-resources-schema-ci-ctest.log), [provenance](evidence/native-mcp-resources-schema-ci-provenance.json). That revision predates registered agent activation above. SDK interoperability, account-resource mapping and full upstream schema conformance remain incomplete; exact OS CPU-timer scheduling is not proved.

The preceding approval-backed MCP source passed [isolated CI run 37610809269](https://github.com/xlang-foundation/xMind/actions/runs/37610809269) at `5b24f26a6ee2dd5e3da06aaee6169b5513a6a980`, all twenty-five native and twenty-seven extension contracts: [CTest evidence](evidence/native-mcp-effects-ci-ctest.log), [provenance](evidence/native-mcp-effects-ci-provenance.json). That revision predates shared-resource migration and schema-worker containment; it does not prove their isolated CI result.

Native approval-backed MCP checkpoint: Release compilation and all **twenty-five** native contracts passed locally at `5b24f26a6ee2dd5e3da06aaee6169b5513a6a980`. Actual independent MCP subprocesses wrote real fixture files only after exact durable approval. Tests cover denied/cancelled/pre-dispatch effects, duplicate identities, exact numeric/schema/config binding, invalid catalogs/arguments/output, lost/error replies after actual effects, workspace quarantine and outcome-storage fault/restart without replay. Native JSON Schema 2020-12 validation and all 176 pinned library/source license hashes passed too. [Complete build/CTest evidence](evidence/native-mcp-effects-local-build-ctest.log), [binary/source provenance](evidence/native-mcp-effects-local-provenance.json). Peer acknowledgement is explicitly distinguished from independent verification. Registered configuration persistence, agent/CLI/view wiring, SDK interoperability, full schema conformance, adversarial evaluation containment and server/account-wide resource locking remain incomplete. [Isolated CI run 37610809269](https://github.com/xlang-foundation/xMind/actions/runs/37610809269) is in progress; this is a local component checkpoint, not full MCP/product readiness.

The preceding native catalog client additionally passed [isolated CI run 37608803467](https://github.com/xlang-foundation/xMind/actions/runs/37608803467) at `fe07a95d5984748aaabbd744a76bda9d75bb45ef`, all twenty-three native and twenty-seven extension contracts: [CTest evidence](evidence/native-mcp-client-ci-ctest.log), [provenance](evidence/native-mcp-client-ci-provenance.json). That revision precedes schema/effect integration and does not prove the new twenty-five-contract source.

Isolated negotiation checkpoint: [CI run 37607766121](https://github.com/xlang-foundation/xMind/actions/runs/37607766121) passed all twenty-two native and twenty-seven extension contracts at `809ef148ba5a16778a0a1a98ca06637394497c00`. Actual independent peers verify modern discovery, legacy error/timeout fallback, retired late discovery replies, initialized notification ordering and each supported legacy revision (`2025-11-25`, `2025-06-18`, `2024-11-05`). [CTest evidence](evidence/native-mcp-handshake-ci-ctest.log), [provenance](evidence/native-mcp-handshake-ci-provenance.json). That revision precedes the peer-request reply helper and catalog client; it does not establish their isolated CI result or model/tool integration.

Native MCP client checkpoint: Release compilation and all twenty-three native contracts passed locally at `fe07a95d5984748aaabbd744a76bda9d75bb45ef`. The owned native subprocess client discovers real peer catalogs with pagination, validates bounded descriptions, handles legacy ping/unsupported methods and retires on malformed or interrupted exchanges without reconnect/replay. Modern/legacy negotiation and transport contracts also passed. [CTest evidence](evidence/native-mcp-client-local-ctest.log), [runtime binary provenance](evidence/native-mcp-client-local-provenance.json). Isolated CI is pending. Tool execution remains absent until policy/journaling integration; full schema validation, resources/prompts and SDK interoperability remain required.

Native MCP request-tracking checkpoint: [CI run 37602231619](https://github.com/xlang-foundation/xMind/actions/runs/37602231619) passed all twenty native and twenty-seven extension contracts at `276164689deacb130a66fce4362ad94d07cd12b7`. Actual compiled correlation fixtures verify bounded admission, out-of-order matching, peer errors, duplicates/unknown IDs, cancellation, initialization cancellation rejection, abandonment and retirement bounds. [CTest log](evidence/native-mcp-requests-ci-ctest.log), [provenance](evidence/native-mcp-requests-ci-provenance.json). Windows stdio/process fixtures subsequently passed [CI run 37603832587](https://github.com/xlang-foundation/xMind/actions/runs/37603832587), all twenty-one native and twenty-seven extension contracts at `a98cf0bc59d0088d9e64a36b43d064b5bcbaf3cc`, including waiting-reader cancellation. [CTest evidence](evidence/native-mcp-stdio-cancellation-ci-ctest.log), [provenance](evidence/native-mcp-stdio-cancellation-ci-provenance.json). Modern/legacy negotiation and peer-request replies subsequently passed locally with the twenty-three-contract client checkpoint above; isolated CI and tool/policy integration remain required.

Native MCP wire checkpoint: [CI run 37601682734](https://github.com/xlang-foundation/xMind/actions/runs/37601682734) passed Release compilation, all twenty native contracts and twenty-seven extension contracts at `f0e44b029d6a28a57a80f77e921641301105a104`. The new real C++ codec passed bounded fragmented JSON-RPC/UTF-8 fixtures, precise IDs, modern request metadata and rejection/EOF behavior. [CTest log](evidence/native-mcp-wire-ci-ctest.log), [provenance](evidence/native-mcp-wire-ci-provenance.json). Added request tracking and Windows owned-process/stdin/stdout transport are subsequent source awaiting their own native runs. This is not full MCP peer/tool integration, A2A or graph completion.

Verified uncertainty inspection checkpoint: [CI run 37600409106](https://github.com/xlang-foundation/xMind/actions/runs/37600409106), revision `cbc5f26c3c1842a54013f94cbf3baf303761b568`, passed all nineteen native and twenty-three extension contracts. The actual production server reopened the fixture journal without a model, inspected bounded raw file bytes through HTTP/CLI/editor host client, rejected another workspace and preserved the uncertain operation/events. [CTest evidence](evidence/native-uncertain-inspection-ci-ctest.log), [provenance](evidence/native-uncertain-inspection-ci-provenance.json). Its packaged runtime now runs in the [actual unseeded sidebar](evidence/vscode-inspection-bundle-launch.json); no inspection workspace or provider is configured in this preview. Attributed resolution and actual populated recovery UI remain incomplete. The new MCP codec is separate source awaiting the twenty-contract native gate.

Recorded-run history checkpoint: the sidebar now exposes each backend run in the selected conversation, preserving full transcript history while choosing older events/operations. Selected-run identity persists by origin/session, and IDs from another conversation are rejected before fetching their details. Older uncertain edits remain inspectable. Selecting historical work preserves observation of another active run and blocks additional submissions. [Twenty-seven extension contracts](evidence/vscode-run-history.log) passed against labeled HTTP/VS Code API/DOM fixtures. The [actual unseeded preview reopened](evidence/vscode-run-history-preview.json) with saved SecretStorage against the existing verified native bundle; [screenshot](evidence/vscode-run-history-preview.png). Actual populated history rendering and live model execution remain separate verification requirements.

Uncertain-edit inspection source checkpoint: native read-only HTTP/CLI access and an explicit model-free inspection workspace now connect the existing raw-file inspector to clients. The VS Code sidebar offers **Inspect actual file**, displays the backend observation and preserves quarantine without any grant/replay/restore action. [Twenty-three extension contracts](evidence/vscode-uncertain-inspection.log) passed locally, including escaped observations and selection invalidation. Native production restart/identity/journal contracts are added and await hosted CI; this entry does not claim their pass or actual uncertain-edit rendering in the interactive preview.

Verified development bundle checkpoint: [CI run 37598517059](https://github.com/xlang-foundation/xMind/actions/runs/37598517059) passed all nineteen native contracts, including selected-model CLI initiation of an actual approved file edit, and twenty extension contracts. Its packaged C++ server and embedded xlang3 runtime now run in the actual local VS Code preview with the existing session database, right sidebar and bottom model selector. [CTest evidence](evidence/native-model-edit-cli-ci-ctest.log), [launch evidence](evidence/vscode-ci-bundle-launch.json), [actual screenshot](evidence/vscode-ci-bundle-sidebar.png). Inference contract peers are synthetic; the preview remains unconfigured for live inference. The development bundle requires external pure standard-library source and is not a complete installer.

Native model/edit integration checkpoint: Release compilation and all **nineteen native contracts** passed on an isolated Windows runner, with stock pinned xlang3 plus the reviewed SQLite prerequisite and allowed standard-library source. Actual model-invoked file application, denial/stale/cancel handling, model selection, supplied usage/measured timings, restart retention and uncertain-file inspection passed using labeled synthetic inference peers. [CI run](https://github.com/xlang-foundation/xMind/actions/runs/37595553681), [complete CTest evidence](evidence/native-model-edit-ci-ctest.log), [provenance](evidence/native-model-edit-ci-provenance.json). All twenty extension contracts also passed. Live-provider/coding completion, new-file/process tools, attributed reconciliation, MCP/A2A, graphs and team/Electron scope remain incomplete. The local preview has no model; its subsequent bundle upgrade is recorded above.

Persistent interactive preview checkpoint: the launcher now uses a normal VS Code extension development host. The old `--extensionTestsPath` bootstrap selected in-memory VS Code storage, losing its server access token on restart; that test mode is retained only for actual tests. Normal-host development bootstrap clears private environment variables, completes activation before opening the view, and records a unique readiness marker. Actual close/reopen reused the saved SecretStorage token against the same native backend without supplying another token. [Reopen evidence](evidence/vscode-preview-reopen.json), [actual screenshot](evidence/vscode-persistent-sidebar.png), and [twenty passing extension contracts](evidence/vscode-preview-persistence.log). No model is configured. Actual read-only diff integration also passed again after activation changes.

Actual VS Code diff checkpoint: `Tools/test-vscode-review.ps1` launches an isolated official VS Code host and runs the product snapshot/diff adapter against labeled test bytes. VS Code 1.140.0 opened a real native diff tab with exact before/after documents; typing and filesystem writes could not alter the read-only snapshot, which stayed clean. [Actual-host evidence](evidence/vscode-native-diff.json). This is editor API integration evidence, not a rendered screenshot or live model/approval/file-effect test. The interactive preview is not seeded with these fixtures.

VS Code reconnect checkpoint: Refresh now rechecks backend health/model capabilities, reloads the selected conversation and resumes observation without creating or replaying an agent run. Configured model selection persists by backend origin across view reopening; removed IDs fall back to the backend default. The composer draft stays in the view. Nineteen deterministic extension contracts pass: [reconnect evidence](evidence/vscode-reconnect.log). This verifies adapter behavior with HTTP/host fixtures; actual backend model reconfiguration/live provider execution remains unverified.

VS Code edit comparison checkpoint: pending file approvals now offer **Compare changes**, opening the recorded before/after snapshots in VS Code's native read-only diff editor. The host fetches and revalidates the reviewed operation before opening; webview-supplied text is ignored, and comparison sends no approval or filesystem write. Snapshot storage is bounded to 32 comparisons/32 MiB per extension activation. Seventeen client/host/renderer fixture contracts pass, including exact snapshot comparison and changed-proposal rejection: [comparison evidence](evidence/vscode-edit-comparison.log). Actual live model-driven proposal/diff interaction remains unverified while the native integration is awaiting compilation.

Right-sidebar chat checkpoint: the real VS Code 1.140 development host renders xMind in the right secondary sidebar, with Explorer left and the composer/model selector anchored below scrolling history. Fifteen client/host/DOM tests cover sanitized Markdown/code, per-response usage badges, missing metrics, approvals, backend-advertised model choices and live-prefix preservation. [Actual unseeded UI screenshot](evidence/vscode-right-sidebar.png) and [test evidence](evidence/vscode-sidebar-renderer.log) verify that scope. No model is configured in the preview. Native per-run model selection, persisted timings/usage and model-invoked edits are pending verification behind the live benchmark build guard; live history/inference and full coding completion remain unverified.

VS Code approval view checkpoint: the view displays recorded operation payloads and before/after text, and sends allow/deny choices through the authenticated host adapter. The host requires a reviewed pending proposal in the selected run and revalidates the exact backend record before granting. Eight deterministic client/host contracts pass, including changed/unreviewed/repeated decision rejection and disposal during revalidation: [approval host evidence](evidence/vscode-approval-host.log). Actual IDE rendering, model-invoked edits, reconciliation and team permissions remain unverified or pending.

Native local approval API checkpoint: authenticated local-owner inspection/decision routes, CLI `operations`/`operation`/`decide`, and the thin VS Code host client now connect to durable exact proposals. The actual compiled server and executor apply approved real-file edits and reject stale/denied effects. [Approval API evidence](evidence/native-approval-api-ctest.log) covers eighteen contracts, including unauthorized/spoofed decisions and absent client effect-mutation routes. Agent write-tool integration, approval UI, reconciliation and team scopes remain pending.

Native approved edit component: `EditExecutor` now connects exact durable before/after proposals, attributed approval and one-use claims to actual file application and outcome journaling. Real file/xlang3 tests cover approval, denial, cancellation, stale plans, duplicates and reopen. A fixture-only outcome-storage fault after the actual edit verifies that the file effect remains observable and restart quarantines the unresolved claim. [Approved edit evidence](evidence/native-approved-edit-ctest.log) covers seventeen native contracts. Public controller authorization/routes, agent write tools, partial-write/process-kill testing and reconciliation remain pending.

Native file application component: the backend-only Windows primitive revalidates the exact file snapshot, applies real in-place changes and checks the resulting bytes. Real disk contracts cover longer/shorter/empty edits, stale content, identity/hash/boundary rejection, pre-write cancellation and competing handles. [Application evidence](evidence/native-file-application-ctest.log) covers the sixteen native contracts. Integrated approval/execution, post-write fault recovery and reconciliation remain pending; model tool definitions still offer only read operations.

Native authorization/planning checkpoint: schema-v3 operation records and `PermissionWaiter` now preserve exact approvals, controller attribution, single-use claims, cancellation/expiry, workspace exclusion and uncertain recovery. Workspace identity, same-handle content snapshots and literal edit planning passed against real filesystem fixtures and independent Node hashes. All sixteen native contracts passed: [native-permission-planning-ctest.log](evidence/native-permission-planning-ctest.log). These are library components; public approval controllers, integrated approved file execution and reconciliation remain required before exposing a write tool.

Native execution/server checkpoint: [AgentRunner and AgentService](native-agent-loop.md) invoke the native provider and actual read tools, with encrypted credential resolution, atomic conversation/state persistence, bounded native workers, HTTP/CLI scheduling, cancellation and run deadlines. All fifteen native contracts passed, including actual editor host client requests to the compiled server. Evidence: [native-execution-server-ctest.log](evidence/native-execution-server-ctest.log). Live inference, coding-task completion and actual editor UI validation remain pending.

Native workspace tools checkpoint: [actual C++ filesystem tools](native-workspace-tools.md) read, list and search authorized Windows workspace files, with handle checks, argument validation and bounded results. Real filesystem fixtures and native agent integration passed. Mutation/process tools, permission policy and complete coding workflows remain required.

Native HTTP/console checkpoint: [xMind Server](native-server.md) exposes session persistence and configured native execution, with authenticated independent HTTP/CLI/editor-host clients. Runs and cancellation are owned by the real engine; manual lifecycle transitions and fabricated client assistant messages are rejected. Read-tool execution, cancellation and durable event observation passed with synthetic inference peers. Live provider and full coding workflows remain required.

Native persistence worker checkpoint: [PersistenceService](persistence-service.md) owns embedded xlang3 and the repository on one thread, serving typed requests from concurrent backend callers. All five native contracts passed, including concurrent writes, queue backpressure, owner recovery, failure cleanup and shutdown draining. It is ready for native HTTP/agent callers; those services and their authentication remain incomplete.

Credential storage checkpoint: the C++ repository now protects and persists credentials through xlang3, with scope/purpose binding, revision-checked rotation/deletion and retired identities. Atomic schema-v1 migration preserves existing messages. All four native contracts passed; the original milestone database also reopened and replayed events after migration. Committed evidence: [credential-repository-ctest.log](evidence/credential-repository-ctest.log). No team authorization or public credential endpoint is claimed.

- Native xMind Server + CLI: shared sessions/run events, lifecycle, authentication and reconnection.
- Coding engine: real model/tool task with explicit approvals, verification and reviewable changes.
- Protocols and graphs: independent MCP/A2A peers, graph branching/pauses/checkpoint recovery.
- Clients: browser UI and VS Code workflows over the local backend, with actual UI validation.
- OSS release: reproducible native builds, scoped parity evidence, documented limitations and end-to-end local coding/protocol/provider acceptance.
- Connection profiles: Local single-user mode plus optional Nexus binding over the shared protocol, scoped identities/state, authenticated local agent/workspace enrollment and real reconnect/lease/recovery acceptance. Nexus owns the private team-server/distributed implementation.

These milestones retain the revised xMind OSS scope. Team-server features, PostgreSQL, WebRTC and Electron belong to Nexus and are excluded. Relevant OpenCode coding parity and agreed broad model/provider support remain required.
