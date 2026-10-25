# Generic remote connection v1 proposal

Status: remote execution, enrollment and worker fencing remain proposed and
not deployed. Native verified-HTTPS negotiation, strict response validation,
encrypted combined credential/grant storage through embedded xlang3/SQLite,
cached authentication/scope observation and scoped session metadata reads are
implemented in the development candidate. The registered Connections subobject
uses ordinary xlang3 SDK values and methods; CLI and native HTTP adapters use
the same native service. Remote mutation routing remains unavailable.

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
  .OpenScope(profile_id, expected_profile_revision, scope_id)     [read only]
      .Identity()
      .Sessions().Open(session_id)                         [metadata only]
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
Its Sessions object exposes only Open; submission, cancellation, history,
events, enrollment and worker execution are not enabled by this binding.
The public identity contains connection/profile/credential revisions, server,
protocol, scope and backend generation, plus `command_routing:false`. It contains
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

`RemoteCommandJournal` is backend-private C++ infrastructure for a typed
`sessions.create` intent. PersistenceService performs transactional compare and
publication through embedded xlang3/SQLite. It enables no SDK capability, network
submission, profile activation or local effect. The connector must independently
validate current authorization before calling it.

The immutable semantic record contains operation, connection, verified origin,
server/scope, profile/credential revisions and title. Canonical validated JSON
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

## Implemented native session-command connector (adapters pending)

Native RemoteConnectionAuthentication now joins the journal to typed session
creation and request observation. This backend service is not yet exposed by
registered SDK methods, CLI or local HTTP routes; no public command capability
is advertised and installed74 is unchanged. Production uses verified HTTPS;
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
