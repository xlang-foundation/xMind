# Generic remote connection v1 proposal

Current acceptance (source d55741e0): the complete hosted177 native gate verifies
trusted-TLS SDK/CLI/HTTP command, run-event and durable-consumer fixtures. The
local gate separately passes179. These are controlled-peer protocol tests, not
acceptance against a deployed Nexus server. See the
[hosted result](evidence/hosted-native-d55741e0-result-20261024.json).
The chronological implementation notes below retain their original narrower
acceptance boundaries. Authoritative snapshot/reset, root-tree recovery,
workspace/effect enrollment, fenced workers and profile-wide Remote activation
remain open. Candidate76 installation and hosted packaging have separate gates.

The backend-private command journal also recognizes a typed `runs.cancel` intent
with session ID, run ID and positive expected run revision. It shares the
existing request-ID namespace and durable unknown-before-send boundary. An
admitted cancellation receipt must name that same run at a revision no older
than the captured revision; it proves request admission only, never worker
retirement. Native typed cancellation exchange and journal-backed reconciliation are implemented
in source, with standard Runs.Cancel and thin SDK/CLI/HTTP adapters.


Status: remote execution, enrollment and worker fencing remain proposed and
not deployed. Native verified-HTTPS negotiation, strict response validation,
encrypted combined credential/grant storage through embedded xlang3/SQLite,
cached authentication/scope observation and scoped session metadata reads are
implemented in the development candidate. The registered Connections subobject
uses ordinary xlang3 SDK values and methods; CLI and native HTTP adapters use
the same native service. Typed captured session creation, run admission,
cancellation, metadata, receipt observation, run-only events and durable event
consumers are implemented in source. Profile-wide Remote activation, root-tree
events and enrollment remain unavailable.

Actual hosted trusted-TLS authentication is verified through separate SDK,
CLI and native HTTP clients, including wrong-credential refusal, one grant per
client, stale retry without another negotiation, encrypted database reopen and
unchanged Local selection. See [official hosted SDK/CLI/HTTP evidence](evidence/hosted-native-264cadb4-trusted-sdk-cli-http.log)
and [terminal hosted result](evidence/hosted-native-264cadb4-result-20261024.json).
Rendered Settings refusal/input paths have separate acceptance. Authentication
never enables remote command routing or creates workspace enrollment.

This remains an OSS client/worker contract proposal for the unimplemented
remote operations below. It makes no claim about any existing Nexus endpoint,
authentication system, database or scheduler. An existing public server
contract, if supplied, must be reconciled before implementation.

## Object boundary

Register the `xmind` root once at backend startup. Keep ordinary xlang3 SDK
objects and method calls between applications and the local backend:

```text
xmind.Connections(owner_token)
  .Authenticate(profile_id, expected_profile_revision, request)  [implemented]
  .Authentication(profile_id, expected_profile_revision)         [implemented]
  .AuthorizedScopes(profile_id, expected_profile_revision)       [implemented]
  .OpenScope(profile_id, expected_profile_revision, scope_id)     [captured scope]
      .Identity()
      .Sessions().Open(session_id)                         [metadata only]
      .Sessions().Create(request_id, title)                [typed creation]
      .Runs().Open(session_id, run_id)                     [run metadata]
      .Runs().Submit(request_id, session_id, expected_session_revision, prompt)
      .Runs().Cancel(request_id, session_id, run_id, expected_run_revision)
      .Requests().Open(request_id)                         [local journal]
      .Requests().Reconcile(request_id)                    [receipt GET]
      .RunEvents(session_id, run_id).Cursor()              [initial cursor]
      .RunEvents(session_id, run_id).Read(cursor)          [run-only page]
      .RunEvents(session_id, run_id).Consumer(consumer_id)
          .Open() / .Read(checkpoint_revision)
          .Accept(checkpoint_revision, cursor)             [explicit acknowledgement]
          .Snapshot(checkpoint_revision)                  [captured baseline]
          .ResumeSnapshot(snapshot_id)                    [explicit stored baseline]
              .Open() / .Read(snapshot_id_or_none, assembly_revision)
              .Observe(snapshot_id)
              .Reset(snapshot_id, assembly_revision, digest)
  .Open(profile_id, expected_profile_revision)                   [Local only]
      .Identity()
      .Sessions() / .Runs() / .Graphs() / .Models() / .Commands()
      .Workspace() / .Providers() / .Contexts() / .Skills()
      .Permissions(session) / .Inbox(session)
      .Events(session, root) / .RunEvents(session, run)
      .Enrollments()                                         [proposed]
      .Workers()                                             [proposed]
```

Existing Local methods keep their current signatures and behavior. Proposed
methods require explicit capability versions before callers use them. There is
no generic `Call(method, payload)` object, `peer.call` dispatcher, `ImportRemote`
wrapper or application IPC value serializer. The xlang3 SDK handles local
object references, value transfer and correlation. The native connector alone
maps typed service operations onto the remote network contract below.

`AuthorizedScopes` is cached metadata observation, not scope binding or live
authorization. Its nine fields are `connection_id`, `profile_revision`,
`credential_revision`, `state`, `server_id`, `protocol_version`, `expires_at`,
`scopes` and `command_routing`. Each scope is exactly `{id, actions}`. Only a
matching unexpired stored grant supplies scopes; absent, stale-profile and
expired states supply an empty list. Credentials and grant IDs/revisions remain
backend-private. The result never selects a profile, negotiates, submits work or
enables command routing. Future scoped operations must independently fence
profile, credential/grant, scope and resource identity at their admission point.

Network JSON is a server protocol, not an alternative IPC serializer. Remote
server references never become caller-selected local object IDs or paths.
Clients retain the captured binding; they cannot redirect it by changing global
connection selection. Existing root compatibility services remain Local.

## Implemented scoped session metadata observation

Capability `remote_scoped_session_read:1` advertises this development API.
`OpenScope` currently requires an authenticated, unexpired grant with exact
`sessions:1` capability and `sessions.read` permission in the selected scope.
Its Sessions object exposes Open and typed Create; Runs exposes metadata Open and
typed Submit. Cancellation, history, events, enrollment and worker execution are
not enabled by these admission/observation methods.
The public identity contains connection/profile/credential revisions, server,
protocol, scope and backend generation, plus `command_routing` (false for read-only grants; true only when the captured
grant supports at least one implemented typed mutation and receipt reads). It contains
no credential or grant ID/revision fields. Local selection remains unchanged.

The connector issues one GET to `/xmind/v1/scopes/{scope_id}/sessions/{session_id}`
over the configured verified HTTPS origin. Opaque resource components are
encoded individually; dot-only components, separators, percent escapes and query
injection are refused. Backend-selected headers are Authorization Bearer,
X-XMind-Protocol-Version, X-XMind-Server-ID, X-XMind-Scope-ID, X-XMind-Grant-ID and
X-XMind-Grant-Revision. Clients cannot supply these headers or grant values.
No redirects or automatic retries occur. The peer must independently authorize
the credential, grant and scope; client-side checks do not replace that boundary.

The response is at most 64 KiB and has exactly this shape:

```json
{"binding":{"server_id":"SERVER","scope_id":"SCOPE","protocol_version":1,"grant_id":"GRANT","grant_revision":1},"session":{"id":"SESSION","title":"Session title","revision":3}}
```

Every binding field and session ID must match the captured request. The title
is bounded to 4096 UTF-8 bytes and the positive resource revision fits a safe
JSON integer. Duplicate keys, extra fields, wrong types and excessive nesting
refuse observation. Profile, credential/grant, scope, expiry and cancellation
are checked before publication; backend generation is checked before the GET
and again before returning data. A profile or credential change during the read
rejects the result. This is a metadata snapshot, not durable command admission,
an event cursor or proof of a deployed Nexus endpoint.

The CLI methods are `connections scope-open ID REV SCOPE` and
`connections scope-session-open ID REV SCOPE SESSION` after the normal
`xmind ipc --endpoint lrpc:PORT` prefix. The local owner/origin-bound HTTP adapter
is POST `/v1/connections/scope-session` with exactly `id`,
`expected_profile_revision`, `scope_id` and `session_id`. Both return native
projections of the same captured service state; neither activates a remote
profile. The installed package is separate from this source checkpoint.

## Trust and authentication

The profile supplies a canonical HTTPS origin. Reject credentials in URLs,
fragments and unexpected query parameters. Verify the transport certificate;
do not follow an authenticated redirect to a different origin. Loopback HTTP
is allowed only in an explicitly configured isolated test peer.

`Authenticate` accepts a native dictionary containing a credential supplied by
the caller over authenticated local IPC. It is never a command-line argument,
public profile field, event, model input or log entry. The first proposal uses
a provisioned bearer credential; browser login, OAuth discovery and refresh
flows are separate future capabilities, not implicit behavior.

The connector negotiates over the verified origin, then stores the credential
encrypted through the existing native/xlang3 SQLite storage boundary. Public
results contain no credential or reusable credential reference. Credential
activation and the verified grant are committed together only if the expected
profile revision still matches. Failure leaves prior active state untouched.

On first authentication the verified endpoint establishes the server identity
pin. Subsequent authentication must match it. An origin or server identity
change requires explicit reauthentication and cannot inherit the old grant.
This proposal uses transport trust plus that pin; it does not claim independent
verification of the user's intended organization on first contact.

## Negotiation and captured identity

Proposed `POST /xmind/v1/negotiate`, authenticated with the enrolled credential:

```json
{"supported_versions":[1],"required_capabilities":{"sessions":1,"runs":1,"events":1},"expected_server_id":null}
```

Successful response fields are exactly `server_id`, `protocol_version`,
`grant_id`, `grant_revision`, `expires_at`, `authorized_scopes` and
`capabilities`. Versions and capability versions are positive integers;
scope entries are opaque server-owned IDs with explicit permitted actions.
The server chooses one offered version and must satisfy every required
capability. Empty scopes, expired grants, malformed fields, a mismatched pin or
unsupported version refuse activation. Capabilities never grant permission.

The v1 response validator accepts only protocol1. Capability versions match
exactly; a higher version is not assumed compatible. `expires_at` is a positive
signed64-bit Unix UTC second strictly later than the supplied current time.
Each authorized scope is exactly `{id, actions}`, with a nonempty action list.
Identifiers/action/capability names use1..256 ASCII letters, digits, `_`, `-`,
`.` or `:`. Limit responses to64KiB,128 scopes,64 actions per scope and128
capabilities; reject duplicate keys, scope IDs/actions, unexpected fields and
excessive nesting. Error text does not echo peer-provided values. These are
proposed schema bounds, not assertions about an existing server.

The local captured identity retains existing `connection_id`,
`profile_revision`, `server_id`, `protocol_version` and `backend_generation`.
The connector additionally retains grant ID/revision, expiry and selected
authorized scope privately. The backend generation fences local SDK objects;
it is not a remote worker fencing token. Public identity exposes no auth grant.

Every remote request carries the negotiated grant ID/revision and exact
server/scope binding, with authentication verified independently by the peer.
Every response confirms server and scope. A mismatch fails before publishing
records to clients. Profile revision changes block new mutations; previously
admitted work remains readable/reconcilable under its original scope while
authorization permits. Expiry/revocation never falls back to Local execution.

## Typed operations and durable receipts

### Native run metadata observation connector

The development connector defines a typed GET at
`/xmind/v1/scopes/{scope}/sessions/{session}/runs/{run}`. It requires
`runs:1` and the exact scope's `runs.read` action independently of session
read or run submission permission. The existing verified HTTPS, bearer and
captured grant headers apply; there are no redirects or automatic retries.
The response is bounded to64KiB and has exactly this shape:

```json
{"binding":{"server_id":"SERVER","scope_id":"SCOPE","protocol_version":1,"grant_id":"GRANT","grant_revision":1},"run":{"id":"RUN","session_id":"SESSION","root_id":"ROOT","state":"running","revision":7}}
```

Binding and requested session/run IDs must match exactly. Root identity is an
opaque resource ID; a child may have a different root. Revision is a positive
JSON-safe integer. State is one of queued, running, paused, completed, failed
or cancelled; unknown states, duplicate keys, extra fields and mismatches refuse.
Grant expiry is checked before and after observation. The captured service also
checks the stored profile and credential/grant revisions before sending and
before publishing a result, including trusted backend exchange results. Existing
OpenScope admission still requires sessions.read; run observation additionally
requires runs.read. Capability remote_scoped_run_read:1 advertises
OpenScope(...).Runs().Open(session_id,run_id). The ordinary SDK subobject returns
exactly binding and run, with the public captured identity and the five run fields.
CLI uses connections scope-run-open ID REV SCOPE SESSION RUN. Native HTTP uses
POST /v1/connections/scope-run with exactly id, expected_profile_revision,
scope_id, session_id and run_id under the existing owner/origin-bound view checks.
All adapters call the native service and fence backend generation. They do not
select a profile. Run submission has a separate typed method below; cancellation,
events and worker retirement remain separate implementation/verification requirements. A terminal run
metadata response does not itself establish physical worker retirement.

Proposed resource paths are relative to `/xmind/v1/scopes/{scope_id}`. Encode
opaque IDs as individual path components; reject ambiguous encodings.

| Native service | Proposed network resource | Required capability |
| --- | --- | --- |
| Sessions | `/sessions`, `/sessions/{id}`, `/sessions/{id}/messages` | `sessions:1` |
| Runs | `/sessions/{id}/runs`, `/sessions/{id}/runs/{run}` | `runs:1` |
| Run cancellation | `/sessions/{id}/runs/{run}/cancel` | `runs:1` |
| Durable command observation | `/requests/{request_id}` | `receipts:1` |
| Root/run event observation | `/sessions/{id}/runs/{run}/events` | `events:1` |
| Enrollments | `/enrollments`, `/enrollments/{id}` | `enrollments:1` |
| Worker leases | `/workers/{id}/leases` | `worker_leases:1` |

Additional services are unavailable until their typed request/response schema
and capability are defined. Do not blindly forward Local provider credential
setup, local filesystem imports or workspace paths to a server.

Mutations use POST with a domain `request_id`, expected resource revision and
typed operation fields. Transport correlation is diagnostic only. The peer
atomically persists the request's canonical semantic fingerprint and admission
receipt before acknowledging. Same ID/same operation returns the original
receipt without repeating effects; same ID/different operation is a conflict.
The fingerprint includes server, scope, actor and operation, excludes transport
correlation, and is computed from validated fields by the owning backend.

Before sending a mutation, local SQLite records its captured identity, request
ID, operation fingerprint and `prepared` state. After send, the outcome is
`admitted`, `rejected` or `unknown`; a timeout after possible send is unknown.
Query the original `/requests/{id}` to reconcile. `not_found` is not permission
to create a new ID: retry only the identical operation under the same ID when
the peer's deduplication contract is available. Never replay a completed tool.

Receipt fields are `request_id`, `operation`, `resource_id`, `resource_revision`,
`state` and the confirmed server/scope identity. A cancellation receipt confirms
request admission, not physical worker retirement. Run observation separately
reports cancellation pending, terminal completion or uncertain effects.

Events use the existing scoped cursor semantics plus remote retention epoch.
The cursor binds server, connection, scope, session, root/run, epoch and sequence.
A gap or expired epoch requires an authoritative snapshot and explicit cursor
reset; it cannot silently skip records or replay a mutation. Reconnect is an
observation operation. Revoked read permission refuses reconciliation visibly.

## Implemented local command journal foundation

### Native run submission and receipt connector

The development native service now admits typed run intents through the durable
journal before calling the connector. Capability remote_scoped_run_submit:1
advertises the ordinary Runs().Submit(request_id,session_id,expected_revision,prompt)
subobject method. Its expected revision must be an actual integer, not a boolean.
CLI uses connections scope-run-submit ID REV SCOPE REQUEST SESSION EXPECTED_REV
PROMPT, or --prompt-stdin in place of PROMPT for exact redirected UTF-8 input
including multiline/CRLF text up to256KiB. HTTP uses POST
/v1/connections/scope-run-submit with exactly id, expected_profile_revision,
scope_id, request_id, session_id, expected_session_revision and prompt. The existing
1MiB HTTP request-body cap remains. Both adapters retain captured generation and
authorization checks. They return only binding and the six public request fields;
the prompt and its digest remain private to native intent/remote receipt validation.
Command routing indicates availability of at least one authorized typed mutation,
not run completion or support for every remote operation.

The connector requires runs:1/receipts:1 and the scope's
runs.submit plus receipts.read actions for POST. Receipt GET requires receipts.read
and the compatible capabilities, independently of current submit permission.

POST `/xmind/v1/scopes/{scope}/sessions/{session}/runs` has exactly request_id,
operation (`runs.submit`), session_id, expected_revision and prompt. Session ID in
the body must agree with its encoded path component. The peer must independently
authorize the actor and atomically validate the expected session revision while
deduplicating the full intent. It must compute prompt_sha256 itself from the exact
UTF-8 prompt bytes, rather than trusting a client-provided fingerprint.

Both POST and GET `/xmind/v1/scopes/{scope}/requests/{request_id}` return exactly
binding and receipt. Binding has the existing five captured fields. A known run
receipt has exactly request_id, operation, session_id, expected_revision,
prompt_sha256 (lowercase SHA-256), state, resource_id and resource_revision. Every
intent field must match the retained request; responses remain bounded to64KiB
without reflecting the prompt. Admitted resources have an opaque run ID and a
positive JSON-safe revision; pending/rejected receipts have empty ID/revision0.
GET not_found remains exactly request_id/state and leaves the journal unknown.
POST not_found is invalid. No state permits automatic resubmission.

The service stores unknown before dispatch, checks captured authorization before
and after the exchange, and retains unknown on lost replies or cancellation after
possible send. A changed credential/profile refuses the observed result without
resetting the intent. A fresh authorized receipt-only grant can reconcile the
original intent after database reopen. Terminal reconciliation is local observation
with no GET or POST. Admission receipts do not establish execution completion or
physical worker retirement. Controlled connector/service fixtures are separate
from native OS-trusted HTTPS and actual remote worker execution evidence.

`RemoteCommandJournal` is backend-private C++ infrastructure for typed
`sessions.create` and `runs.submit` intents. A C++ variant distinguishes session
title from run session/prompt/expected-session-revision; callers cannot supply an
arbitrary operation string. The run prompt must be nonempty valid UTF-8, at most
256KiB, and the expected session revision must be a positive JSON-safe integer.
Both actions share the same durable request-ID namespace: a session creation
and a run submission cannot reuse one ID for different intent. Existing canonical
session record bytes remain unchanged. This journal extension does not enable
network run submission or grant authorization for it.

PersistenceService performs transactional compare and
publication through embedded xlang3/SQLite. It enables no SDK capability, network
submission, profile activation or local effect. The connector must independently
validate current authorization before calling it.

The immutable semantic record contains operation, connection, verified origin,
server/scope, profile/credential revisions and the typed action fields (session title or run session/revision/prompt). Canonical validated JSON
bytes provide exact semantic equality; no credential or auth grant is stored.
The local credential revision is an authorization snapshot fence, not a remote
actor ID. The peer must derive its actor from authenticated credentials and own
its separate deduplication fingerprint. Backend generation is deliberately not
part of durable intent identity, so restart cannot manufacture a new request.

A globally unique domain request ID creates `prepared`; reusing it with another
intent conflicts, including across connections/scopes. A single atomic compare
changes prepared to `unknown` BEFORE any possible send. Only the winner may
send. A crash in that interval conservatively leaves unknown even if nothing
was sent. Reopen, duplicate prepare and competing sender attempts cannot reset
unknown or authorize another send. No not-found retry transition exists yet.

A typed matching server/scope/request/operation resolution can settle unknown
to admitted with resource identity/revision, or rejected without an admitted
resource. Repeating the exact terminal observation is idempotent; changing it
conflicts. Receipt admission does not mean execution completion or worker exit.
Network response validation, current-grant fencing, lost-reply observation and
SDK/CLI/HTTP command methods remain required before remote routing is enabled.
[Focused real SQLite evidence](evidence/remote-command-journal-20261024.json).

## Implemented native session-command connector

Native RemoteConnectionAuthentication now joins the journal to typed session
creation and request observation. Registered SDK subobjects, CLI and local HTTP
adapters now expose it in source; installed74 remains unchanged. Production uses verified HTTPS;
only trusted backend tests may select the exact loopback policy.

Creation requires sessions:1 and receipts:1, plus sessions.create and
receipts.read in the captured scope (which currently also requires sessions.read).
Before POST the exact intent is prepared and a single durable unknown claim wins.
Preflight cancellation before preparation sends nothing; failure after claiming
leaves unknown conservatively, even if no bytes reached the server. No exception,
missing receipt or transport error authorizes another POST.

POST /xmind/v1/scopes/{scope}/sessions has exactly this body:

```json
{"request_id":"REQUEST","operation":"sessions.create","expected_revision":0,"title":"TITLE"}
```

Expected revision zero denotes a new resource, never replacement. The peer must
atomically authorize its authenticated actor/scope, deduplicate the exact request
ID and semantic operation, and admit a durable receipt before acknowledging.
The OSS connector does not implement the private server or establish that any
existing Nexus endpoint satisfies this protocol.

GET /xmind/v1/scopes/{scope}/requests/{request_id} observes only that request.
It requires current receipts.read authorization. A fresh captured grant can
reconcile an old intent after credential rotation, provided connection, origin,
server and scope still match. Original intent/profile/credential revisions stay
unchanged. A retired/stale grant refuses before lookup; it cannot fall back Local.

Every response has exactly binding and receipt. Binding is the same exact
server/scope/protocol/grant identity used by metadata reads. Known receipts have
exactly request_id, operation, expected_revision, title, state, resource_id and
resource_revision, confirming the original request ID/title and revision zero.
Admitted has an opaque resource ID and positive safe-integer resource revision.
Rejected and pending have empty resource ID and revision zero. Pending remains
unknown locally. Admission does not imply model/tool execution completion.

A GET miss instead has exactly this receipt, because the peer cannot know an
unreceived title. Binding validation still applies:

```json
{"request_id":"REQUEST","state":"not_found"}
```

Not-found from POST, extra/missing fields, wrong ID, duplicate keys, mismatched
identity/title/revisions, oversized responses and redirects refuse. GET not-found
preserves unknown and never resets prepared or retries. Terminal matching
receipts settle the original journal entry; all other outcomes preserve it.
Profile/credential changes or cancellation during observation prevent publication.

The real loopback transport and real SQLite/injected-service tests are separate
proofs; neither claims a deployed server, native trusted-TLS mutation acceptance
or public SDK routing. [Native focused evidence](evidence/remote-session-command-native-20261024.json).

## Public captured command adapters (source candidate)

Capabilities remote_scoped_session_create:1 and remote_request_receipts:1
advertise the methods. Ordinary registered xlang3 objects expose
OpenScope(id, revision, scope).Sessions().Create(request_id, title) and
OpenScope(...).Requests().Open(request_id) / Reconcile(request_id).
Create's first argument is the durable domain request ID, not a chosen remote
session ID or an IPC call ID. The server chooses the returned resource ID.
Open is a local journal observation; Reconcile performs the original receipt
GET only for unknown state. A terminal record never sends another POST or GET.

Each result is exactly binding and request. Request has request_id, operation,
state, revision, resource_id and resource_revision. The original title, private
intent, credential, grant ID/revision and remote error text are not projected.
Backend lifetime/generation and current scope/grant checks fence these objects.
The binding command_routing flag is true only when this scope supports typed
session creation plus receipt reads; it does not advertise absent run methods.
The connection catalog and generic Connections.Open remain Local-only.

CLI after xmind ipc --endpoint lrpc:PORT connections:

- scope-session-create ID REV SCOPE REQUEST TITLE
- scope-request-open ID REV SCOPE REQUEST
- scope-request-reconcile ID REV SCOPE REQUEST

Owner or origin-bound local view HTTP routes (POST, no query):

- /v1/connections/scope-session-create: exactly id, expected_profile_revision,
  scope_id, request_id, title.
- /v1/connections/scope-request and /v1/connections/scope-request-reconcile:
  exactly id, expected_profile_revision, scope_id, request_id.

These are thin native service adapters, not an application IPC dispatcher or
serializer. Local profile selection is unchanged. Source qualification verifies
seeded terminal SQLite receipts through SDK/HTTP and separate-client refusal
paths; actual trusted-TLS creation/lost-reply reconciliation through each public
adapter remains pending hosted acceptance. No new package is installed.
[Adapter scope and retained regression](evidence/remote-session-command-adapters-20261024.json).

## Explicit workspace and effect enrollment

`Enrollments.Create(request)` proposes request ID, authorized scope ID,
expected scope revision, local workspace/authority IDs and exact effect classes
offered by the user (for example read, approved edit or approved process).
Local canonical paths, credentials and file contents are excluded from public
enrollment metadata. Membership changes invalidate the prior authority proof.

The local backend checks workspace identity and user approval before sending;
the peer independently authorizes enrollment. Both checks are required. The
receipt binds enrollment ID/revision, server/scope, workspace authority, worker
identity, allowed effects and expiry. Viewing shared sessions creates no
enrollment. Server capabilities cannot expand the locally approved effects.

`Enrollments.Open(id)` observes state. `Revoke(id, expected_revision, request_id)`
durably closes new local effects before network notification. An unavailable
peer leaves notification pending, not enrollment active locally. Reopening or
expanding effects requires a new explicit authorization revision.

## Workers and fencing

Workers use the registered `Workers()` subobject through ordinary xlang3 IPC.
The native backend owns remote polling, authentication, lease verification,
local admission and receipts. A worker cannot supply its own workspace root,
grant, fencing epoch or allowed effect set.

A lease contains server/scope, enrollment ID/revision, worker ID, assignment ID,
run/operation IDs, monotonically increasing fencing epoch, expiry and immutable
operation fingerprint. The server owns assignment and epoch allocation. Before
each effect the local backend checks the complete binding, current enrollment,
lease validity, workspace authority and existing native permission policy.
Clock conversion uses a conservatively measured validity interval; wall-clock
rollback cannot extend a lease. An unverifiable interval refuses new effects.

The local durable effect key is `(server_id, scope_id, assignment_id,
operation_id)`. Duplicate delivery observes the existing receipt. A higher epoch
does not authorize repeating an already completed effect. A stale epoch cannot
start an effect or commit a replacement result. A renewal must retain identity
and fingerprint and is persisted before extending local validity.

Before a new physical effect, the peer must atomically admit an effect-start
receipt against the current epoch, enrollment and deduplication key. The local
backend persists that receipt before execution. No new remote-assigned effect
starts offline. A lost start reply is reconciled before execution. Server
revocation blocks subsequent starts; it cannot instantly stop a previously
authorized effect on a disconnected machine. The server contract must reject
replacement starts while a prior start is unresolved, including on another
worker. A local journal or fencing integer alone cannot provide that guarantee.

Expiry/revocation stops new effects and requests cancellation of active work.
An already-started external effect may be uncertain; it cannot be declared
undone or safely reassigned merely because the lease expired. The server must
reconcile the prior effect receipt before authorizing replacement. Restart
loads receipts and reconciles the exact prior assignment; it does not invoke
the tool again to learn whether it ran. Private scheduling policy is excluded.

## Error and implementation acceptance

Typed failures distinguish unauthenticated, forbidden/revoked, expired grant,
unsupported protocol/capability, server mismatch, stale revision, duplicate
conflict, unavailable, cursor gap, stale lease and unknown effect. Retrying a
read differs from retrying a mutation. UI adapters display the authoritative
state and never switch profile or grant implicitly.

Implementation must preserve current Local behavior and add these proofs:

1. Controlled authenticated TLS peer: successful negotiation, certificate and
   server mismatch, wrong/expired/revoked credential, incompatible version,
   missing capability, redirected origin and stale profile refusal.
2. Separate SDK clients: ordinary captured methods route to the peer, two views
   stay isolated, selection does not transfer admitted work, retired local
   generation refuses. No custom SDK serialization or generic RPC dispatcher.
3. Real SQLite receipts: lost admission reply, duplicate same/different body,
   restart before/after send and reconciliation without duplicate model/tool
   effects. Unknown outcomes remain visible when the peer is unavailable.
4. Two actual worker processes: explicit enrollment, duplicate assignment,
   renew/expire/revoke, stale epoch and reassignment; completed effects remain
   single, unresolved external effects stay uncertain until reconciled.
5. Browser/CLI/extension binding presentation and captured commands, then the
   complete native/client gates and qualified packaging/preservation. Controlled
   peers establish protocol behavior, not deployed Nexus compatibility.

Architecture review should settle the proposed bearer bootstrap, endpoint
namespace and exact typed schemas before production remote activation. This
document supplies a reviewable design. Only the authentication APIs identified
as implemented above are current capabilities; proposed routing/enrollment
APIs are not advertised as available.

### Native cancellation connector and journal-backed service

`cancel_remote_run` POSTs to `/xmind/v1/scopes/{scope}/sessions/{session}/runs/{run}/cancel`
with exactly `request_id`, `operation: "runs.cancel"`, `session_id`, `run_id` and
`expected_revision` (the captured run revision). It requires `runs:1`,
`receipts:1`, `runs.cancel` and `receipts.read`; submit permission cannot authorize
cancellation. The eight-field receipt binds these same five fields plus `state`,
`resource_id` and `resource_revision`, inside the standard five-field grant binding.
Admitted resource ID must equal the target run and its revision cannot regress.
Pending/rejected receipts have empty resource ID and revision zero; only GET may
return the exact two-field request-ID/not_found observation.

`observe_remote_cancel_request` uses the original `/requests/{request_id}` GET
with no request body, requiring receipt permission rather than cancel permission.
Both connectors retain verified HTTPS by default, bounded64KiB replies, strict
keys/types/duplicate rejection, individual opaque path encoding and no redirect
or automatic retry. The backend-private `cancel_run` service shares the existing
durable unknown-before-send journal and fences captured profile/credential/grant
before and after exchange. A fresh receipt-only grant can reconcile the original
intent after credential rotation without changing that intent or resending POST.
Admitted cancellation does not establish terminal run state or worker retirement.
The public adapters are described below; positive native TLS cancellation and
actual remote workers remain separate verification requirements.

### Captured cancellation adapters

Capability `remote_scoped_run_cancel:1` advertises the typed method
`OpenScope(...).Runs().Cancel(request_id, session_id, run_id, expected_run_revision)`.
It uses the ordinary registered xlang3 Runs subobject and existing six-field
request receipt projection. Expected revision must be a positive JSON-safe
integer; booleans refuse. Captured lifetime, profile, credential/grant, scope
and backend generation remain fenced around the native operation.

CLI: `xmind ipc --endpoint lrpc:PORT connections scope-run-cancel ID REV SCOPE REQUEST SESSION RUN EXPECTED_RUN_REV`.
HTTP: owner/origin-bound `POST /v1/connections/scope-run-cancel` accepts exactly
`id`, `expected_profile_revision`, `scope_id`, `request_id`, `session_id`,
`run_id`, `expected_run_revision`, with no query parameters. Replies contain
only captured binding and public receipt metadata. The command-routing flag
recognizes an authorized cancellation-plus-receipt grant, but never activates
a Remote profile or implies worker retirement. Positive trusted-TLS cancellation
and actual remote worker behavior remain separate acceptance requirements.

### Native run-event read primitive

The backend-only `read_remote_run_events` connector uses the scoped session/run
`/events` GET, requiring `events:1` and `events.read`. Initial cursor has empty
epoch and sequence zero and sends no query. Subsequent reads send exactly
`?epoch=EPOCH&after=SEQUENCE`; opaque colons are encoded once. Connection ID is
local cursor metadata retained by the connector, not remote authority. The captured service validates it against the exact profile/binding before send.

Responses have exactly `binding`, `status`, `cursor`, `events`. Binding is the
existing five-field server/scope/protocol/grant identity. The wire cursor has
exactly server_id, scope_id, session_id, run_id, epoch and sequence. All namespace
fields must match; epoch is opaque and sequence is a nonnegative JSON-safe integer.
At most256 events fit a64KiB response. Each event has exactly sequence, run_id,
kind and object-valued payload; sequence strictly increases beyond the submitted
cursor, run_id must equal the requested run and kind is bounded to128 bytes.
The page cursor must equal its last returned sequence, or the original sequence
for an empty page. Global sequence holes are valid for a filtered run stream.

Status `gap` or `epoch_expired` requires an empty event list and raises a typed
reset-required result. An unexpected epoch change under `ok` also refuses.
Neither case publishes a new cursor or retries/resets automatically. Unknown
statuses, duplicate keys, extra fields, malformed bindings or out-of-order/foreign
records fail closed. This primitive does not itself expose SDK/CLI/HTTP event capability, durable
cursor publication, authoritative snapshot/reset workflow or positive native TLS
event acceptance.

### Captured run-event service

Backend `run_event_cursor(scope, session, run)` creates a zero/empty-epoch cursor
only after validating the captured profile/credential/grant and events.read.
`read_run_events` compares connection/server/scope/session/run against that exact
captured scope before dispatch. Invalid ownership, cursor shape or pre-send
cancellation sends nothing. After exchange it rechecks cancellation, profile,
credential/grant and permission before returning any page. Typed backend results
are independently checked for namespace, epoch, count/bytes, payload objects,
strict order and last-record sequence.

Inputs are immutable; a gap or failure cannot publish an advanced cursor. Fresh
explicitly authenticated scopes can resume the same matching cursor after
credential rotation; retired scopes refuse. The service neither persists cursors
nor resets retention gaps. SDK/CLI/HTTP event subobjects, backend-generation
fencing in those adapters, authoritative snapshot/reset and positive native TLS
event verification remain separate work.

### Captured public run-event adapters

After explicit authentication and OpenScope, use ordinary SDK subobjects:

```text
stream = xmind.Connections(owner).OpenScope(id, revision, scope).RunEvents(session, run)
cursor = stream.Cursor()
page = stream.Read(cursor)
// Accept/process the returned records before retaining page.cursor for a later read.
```

Cursor has exactly connection_id, server_id, scope_id, session_id, run_id, epoch,
sequence and backend_generation. Initial epoch is empty and sequence zero. Read
requires an explicit cursor; unlike an initial adapter null, a supplied cursor
never implicitly rebinds or resets. Backend generation is checked before dispatch
and before publishing a page; retained SDK objects refuse after owner retirement.
The cursor is a domain dictionary transferred by xlang3 SDK serialization.

CLI: `connections scope-run-events ID REV SCOPE SESSION RUN JSON_CURSOR|null`
through the usual `xmind ipc --endpoint lrpc:PORT` entry point. HTTP:
`POST /v1/connections/scope-run-events` requires exactly id,
expected_profile_revision, scope_id, session_id, run_id and cursor. A null cursor
requests the initial cursor; otherwise the eight-field cursor is required. HTTP
retains local owner/origin authorization and rejects query parameters. Responses
have binding, cursor and events; each event has sequence, run_id, kind and an
object payload. The adapters return `remote_cursor_reset_required` for retention
gaps or expired/changed epochs (HTTP409), without an advanced cursor or retry.

Capability `remote_scoped_run_events:1` describes adapter support, not permission
or automatic Remote activation. Current grant must independently authorize
events:1/events.read and the captured session/run namespace. SDK cursor/refusal
and HTTP refusal integration plus backend positive pages have focused evidence.
Positive public event delivery over native trusted TLS remains pending. Durable
cursor acceptance, authoritative snapshot/reset and root-tree streams remain open.

### Durable observation checkpoint foundation

Backend-private RemoteEventCheckpoints binds a consumer ID to exact origin,
connection, server, scope, session and run. Its initial accepted cursor is zero
with empty epoch. stage(expected_revision, page) durably retains one validated
page without advancing accepted; accept(expected_revision, cursor) advances only
the exact staged cursor. Identical restaging and repeated acknowledgement return
the original state. Competing/different pages and stale acknowledgements conflict.
The store never resets epochs, fetches events, retries commands or executes work.

A returned or staged page is not automatically consumed. Pending records survive
restart so an interrupted consumer can recover its unacknowledged page. Empty
resume pages leave the revision unchanged. Cursor namespace, strict event order,
object payloads,256-record/64KiB logical page bounds and JSON-safe integers are
validated; gaps or epoch replacement still require authoritative snapshot/reset.
Persistence uses the native PersistenceService compare-and-set through embedded
xlang3 SQLite. No credentials, grants or local backend generation are persisted
as authority in these observation records.

This primitive is not authorization: future captured-service integration must
freshly validate current profile, credential/grant and events.read before
publishing a retained page or accepting its acknowledgement. SDK/CLI/HTTP
consumer methods and the authoritative snapshot/reset workflow remain open.

### Captured event consumer service

The native authentication service now owns open_run_event_consumer,
read_run_event_consumer and accept_run_event_consumer. Consumer identity is
derived from the captured profile/origin plus authorized session/run. Every
operation checks current local profile, credential/grant and events.read before
and after publication. Returning a saved page does not bypass these checks, but
it does not perform a live remote revocation probe. An expired or locally
revoked grant refuses cached data and acknowledgements.

Read requires the current checkpoint revision. A pending page is returned from
SQLite without another GET. Otherwise the existing native event reader validates
the network page and post-response authorization before staging it. Cancellation
or authorization change after the response prevents staging; any failure after
a stage cannot implicitly accept it. Empty resume does not advance revision.
A fresh explicitly authorized scope may recover the same matching consumer after
credential rotation. Accept requires the exact staged revision/cursor and never
executes or repeats a remote command. Its repeated acknowledgement is idempotent.

Three database lifetimes have local focused evidence. Public SDK/CLI/HTTP
consumer adapters, authoritative snapshot/reset, root-tree checkpoint recovery
and native trusted-TLS consumer acceptance remain separate work.

### Public durable event consumers

The standard SDK event object now exposes a durable consumer subobject:

```text
consumer = scope.RunEvents(session, run).Consumer(consumer_id)
state = consumer.Open()
state = consumer.Read(state.revision)
// If state.pending is present, process those records, then explicitly acknowledge:
state = consumer.Accept(state.revision, state.pending.cursor)
```

Open returns binding, consumer_id, revision, accepted and pending. Consumer(id)
explicitly opens/creates the consumer; another binding cannot reuse its ID.
Accepted and pending cursors use the eight public generation-bound fields.
Pending is null or a cursor/events page. Read returns an existing pending page
without another GET; otherwise it stages a validated native observation. Reading
does not accept the page. Empty resume leaves pending null and revision unchanged.
Accept requires the exact pending revision/cursor; repeated acknowledgement is
idempotent. Recovery can redeliver unacknowledged observations, so consumers must
handle their event sequences idempotently. This never retries tool/model effects.

CLI commands after `xmind ipc --endpoint lrpc:PORT connections`:

```text
scope-run-event-consumer-open ID REV SCOPE SESSION RUN CONSUMER
scope-run-event-consumer-read ID REV SCOPE SESSION RUN CONSUMER CHECKPOINT_REV BACKEND_GENERATION
scope-run-event-consumer-accept ID REV SCOPE SESSION RUN CONSUMER CHECKPOINT_REV JSON_CURSOR
```

Owner/origin-bound HTTP POST routes end with
`scope-run-event-consumer-open`, `scope-run-event-consumer-read` and
`scope-run-event-consumer-accept` under /v1/connections/. All require exactly
id, expected_profile_revision, scope_id, session_id, run_id and consumer_id.
Read additionally requires expected_checkpoint_revision and
expected_backend_generation. Accept instead adds expected_checkpoint_revision
and cursor. Query parameters and extra fields refuse. Revision values are
positive JSON-safe integers; booleans refuse. The SDK retained object cannot
survive owner retirement; CLI/HTTP reads compare the caller's observed backend
generation before accessing the consumer. Accept independently verifies the
cursor generation. Fresh explicit Open obtains a new observation envelope after
reconnect, never automatic reset or profile fallback.

Capability `remote_event_consumers:1` advertises these adapters, not grant
authority. Actual SDK/HTTP cached-page recovery and acknowledgement have focused
evidence. Native trusted-TLS consumer delivery, positive CLI acknowledgement,
authoritative snapshot/reset and root-tree checkpoint recovery remain open.

### Snapshot page decoder (native primitive; not a reset API)

The native decoder now defines a bounded run-only snapshot page. This is a new
network-domain contract, not an application IPC serializer. No snapshot method
is advertised through the SDK or negotiation yet. Native one-page retrieval is
implemented, as is captured native service integration. Durable assembly/reset
and public adapters remain open.

The response has exactly `binding`, `snapshot`, `records` and `complete`.
Binding uses the existing server/scope/protocol/grant identity fields. Snapshot
has exactly `id`, `server_id`, `scope_id`, `session_id`, `run_id`, `epoch`,
`sequence`, `total_records` and `offset`. Initial offset is zero. Continuation
must retain snapshot ID, resource, epoch, sequence and total count, with offset
equal to the previous offset plus its record count. A new epoch may replace an
expired epoch; the same epoch cannot regress behind the caller's observed
sequence. This does not itself publish or accept the replacement cursor.

Records have exactly `ordinal`, `kind`, `id`, `revision` and `payload`.
Ordinals are contiguous. Kind is run, message, operation, decision, worker or
budget; the first record is the exact requested run, and later records cannot
be another run. Revisions are positive JSON-safe integers, payloads are objects,
and duplicate kind/ID pairs within a page refuse. These are observational
projections; a decision or worker record never grants local effect authority.
Unknown kinds refuse until a compatible schema is negotiated.

Each page is at most64KiB/256records, with a positive total at most4096. Complete
is true exactly when the page reaches that total. Depth and duplicate JSON keys
are checked. The decoder requires snapshots:1/runs:1/events:1 and explicit
snapshots.read/runs.read/events.read in the captured scope. Snapshot read
permission must cover the peer's entire authorized projection; it cannot be
inferred from ordinary metadata read permission.

The native connector issues one GET to
`/xmind/v1/scopes/{scope}/sessions/{session}/runs/{run}/snapshot`.
Continuation adds exactly `?snapshot_id={id}&offset={next_offset}`. Existing
binding headers and bounded verified-HTTPS transport apply. There are no
redirects, automatic page loops or retries. Resource/continuation/grant checks
precede dispatch; response validation checks current grant expiry. Actual
positive snapshot delivery currently has controlled-loopback evidence only.

The captured native service derives the resource from its captured profile and
scope, validates permission before the exchange, and reloads profile/credential/
grant state before publishing. It independently validates typed backend replies,
including payload size, depth and duplicate-key checks. Profile edits, credential
rotation or cancellation during an exchange prevent publication. These local
authorization fences do not probe live peer-side revocation. A fresh explicitly
authorized scope can observe again after credential rotation; it never resets a
consumer checkpoint. [Captured-service evidence](evidence/remote-snapshot-captured-service-20261025.json).

Still required: positive native trusted-TLS snapshot verification,
retained-object backend-generation fencing in public adapters,
durable bounded assembly across all pages, cross-page duplicate/refusal checks,
explicit snapshot acceptance and atomic checkpoint reset. A partial snapshot
must leave the old accepted cursor untouched. No automatic event retry, model
or tool replay, root-tree completeness, worker retirement or Nexus deployment
is established by page validation. [Focused evidence](evidence/remote-snapshot-page-validator-20261024.json).

### Durable snapshot assembly (backend-private primitive)

RemoteRunSnapshots stages observation pages in SQLite through PersistenceService
and embedded xlang3. An assembly captures consumer/origin/run identity, the
original accepted cursor and checkpoint revision. Its storage key binds consumer
and snapshot ID; observing the same ID under a changed origin, resource or
baseline refuses. This captured baseline is data for a future reset CAS, not
proof that the event checkpoint is still current.

Stage starts at expected assembly revision0. Pages must retain the same
snapshot ID, epoch, sequence and total; offset must equal the accumulated record
count. The last identical page can be retried with its original expected
revision without appending it again. Different competing pages use SQLite CAS;
only one wins. Cross-page kind/ID duplicates refuse. Complete is true only when
all advertised records are stored. There is no implicit replacement of an old
snapshot ID or loss of its partial state.

Incoming pages retain the256-record/64KiB bounds. Complete assembly is bounded
to4096records and4MiB serialized state, with depth/duplicate-payload checks and
canonical persisted-record validation. Partial and complete assemblies survive
database reopen. This store neither fetches pages nor authorizes publication,
accepts a snapshot, resets an event checkpoint, or retries model/tool work.
Captured-service integration and explicit authorized atomic checkpoint reset
remain required. [Store evidence](evidence/remote-snapshot-store-20261025.json).

### Explicit checkpoint reset (backend-private primitive)

RemoteEventCheckpoints.reset takes the captured snapshot baseline, snapshot ID,
expected assembly revision and complete-content SHA256 digest. It reads that
assembly from SQLite itself; caller-supplied records or a replacement cursor
cannot authorize a reset. The digest covers the consumer/baseline, snapshot ID,
watermark, total and complete ordered records. Partial assemblies refuse.

The current event checkpoint must still match the captured baseline revision
and accepted cursor, and have no pending event page. Reset performs one SQLite
compare-and-swap, advancing the accepted cursor and retaining snapshot ID,
digest, assembly revision and original checkpoint revision as a reset receipt.
The complete assembly remains stored. An immediate duplicate acknowledgement
returns the same checkpoint; after further event progress it refuses instead of
rewinding. Competing acknowledgements for different snapshots have one winner.

No reset receipt is added to old checkpoint records merely by opening them.
Existing event stage/accept operations preserve a reset receipt when present.
New-epoch event observation can continue after reset. These persistence rules
are not authorization or a public reset API: captured-service permission checks,
retained-object generation fences and SDK/CLI/HTTP/UI methods remain required.
[Reset evidence](evidence/remote-snapshot-reset-20261025.json).

### Captured snapshot recovery service

The native service now exposes capture_run_snapshot, observe_run_snapshot,
read_run_snapshot_assembly and reset_run_snapshot. These backend methods are
not root-object IPC dispatch or public SDK methods. Capture derives the
consumer's current accepted cursor/revision itself and refuses pending pages.
An assembly read requires that exact baseline to remain current before fetch,
before staging and before publication. Profile/credential/grant permission is
checked around those operations as well.

The first read has no server snapshot ID and expected assembly revision0.
Known-ID reads require the current revision, or the immediately previous
revision to recover a staged response without another GET. Complete assemblies
return cached state. Otherwise one continuation page is fetched and staged;
there is no automatic page loop or retry. Losing the first response before
learning its server snapshot ID is not an exactly-once observation guarantee.
Old partial assemblies are preserved rather than silently overwritten.

Observe and explicit digest reset require current snapshot permission even for
cached content. A fresh explicitly authorized scope may recover after credential
rotation. A reset or observation write may persist before a post-write
cancellation/authorization check refuses publication; fresh authorized
observation resolves that local metadata result. It never retries a remote
model/tool effect. The primitive's atomic baseline/digest checks still govern
reset and delayed acknowledgements cannot rewind subsequent events.

Public retained subobjects, backend-generation fences, CLI/HTTP/UI methods,
positive native trusted-TLS snapshot acceptance and root-tree recovery remain
separate requirements. [Integrated native evidence](evidence/remote-snapshot-recovery-service-20261025.json).

### Standard SDK snapshot subobjects

SDK capability remote_run_snapshots:1 now advertises the retained Snapshot
subobject under a captured remote event consumer. The existing root is registered
once; these are ordinary xlang3 SDK objects and methods. No application IPC
dispatcher, serializer, import wrapper or additional listener is introduced.

```text
consumer = scope.RunEvents(session, run).Consumer(consumer_id)
snapshot = consumer.Snapshot(checkpoint_revision)
baseline = snapshot.Open()
state = snapshot.Read(None, 0)
state = snapshot.Read(state.snapshot_id, state.revision)  // one next page
state = snapshot.Observe(state.snapshot_id)              // saved observation
// Apply the complete observation idempotently before explicitly acknowledging:
result = snapshot.Reset(state.snapshot_id, state.revision, state.digest)
```

Open returns binding, consumer_id, checkpoint_revision and accepted cursor.
Read/Observe return binding, consumer_id, checkpoint_revision, accepted,
snapshot_id, revision, cursor, total_records, records, complete and digest.
Record fields are kind, id, revision and payload. Binding and cursors include
the backend generation; private origin, credential and grant fields are omitted.
Reset returns binding, checkpoint and snapshot_reset. The reset receipt contains
snapshot_id, digest, snapshot_revision and original checkpoint_revision.
Boolean revisions refuse. None is the SDK's ordinary null value.

A client that knows the snapshot ID may explicitly call
`fresh_consumer.ResumeSnapshot(snapshot_id)` after reconnect. The backend loads
the original baseline from SQLite and validates its consumer/origin/resource
under the fresh authorized scope. The resulting object has a new generation;
it does not revive the old object. Observe can return preserved content after a
reset. An immediate duplicate Reset remains idempotent if the checkpoint has
not advanced; later progress causes refusal rather than rewind. Open and Read
require the captured baseline still to be current.

Separate native host/client processes verify payload/digest transfer, explicit
reset, owner retirement/rebind, fresh-object resume and database reopen. Cached
snapshot content is seeded for this SDK fixture; it is not positive network or
trusted-TLS evidence. SDK lifetime and permission checks apply to every method.
CLI/native HTTP/UI routes remain open, so the HTTP capability endpoint does not
yet advertise remote_run_snapshots. [SDK evidence](evidence/remote-snapshot-sdk-20261025.json).
