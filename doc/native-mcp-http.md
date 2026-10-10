# Native MCP HTTP transport

The latest [native login service and Settings controls](native-mcp-login.md) pass
the complete **113/113** local native and **257 extension / 55 browser** gates.
Real native HTTP/View and browser adapter/controller checks cover TLS-negative
login and grant/status boundaries; trusted HTTPS positive login, registration,
refresh/revocation and installed/rendered current acceptance remain unfinished.
[Exact checkpoint evidence](evidence/native-oauth-service-local.json).

The configured native HTTP tool component has passed actual approved JSON/SSE
effects, denial, lost-reply uncertainty and SQLite restart against an official
SDK peer. OAuth discovery metadata and challenge parsing have passed the native
transport contract. Full MCP HTTP/OAuth, current installed-view acceptance and
hosted packaging remain separate requirements. The complete revised local gate
passes **110/110** in **371.74 seconds** with **2438** unchanged inputs. The installed
package predates this implementation. [Raw evidence and exact scope](evidence/native-mcp-oauth-local.json).

The implementation now separates `McpToolClient` from `McpStdioClient`.
The existing production `McpToolRegistry` receives the transport-neutral owner;
the stdio binding implements it. Discovery remains public metadata access,
while `tools/call` remains private to the journal-backed registry. Dispatch
failure still carries whether the request may have been sent, its request ID
and any response bytes needed for effect reconciliation. The interface supplies
no approval authority, outcome setter or automatic request replay.

The existing actual MCP effect contract now passes its real stdio peer through
that interface. Compile-time assertions require an abstract base, a shared
deadline type, noncopyable stdio ownership and no public tool dispatch on either
type. Native compilation and the actual stdio/HTTP effect contract pass. The
earlier accepted `977e945` package predates this change.

## Protocol and ownership

The native wire codec already pins modern `2026-07-28`, with explicit older
negotiation. HTTP must implement those distinct protocol eras too; this is
peer interoperability, not product-format migration. The
[modern HTTP specification](https://modelcontextprotocol.io/specification/2026-07-28/basic/transports/streamable-http)
uses request metadata/mirrored headers, POST JSON or request-scoped SSE, and
POST notification subscriptions. It has no session IDs, standalone GET or
Last-Event-ID resumption. Closing a response stream cancels its MCP request.
The [older HTTP specification](https://modelcontextprotocol.io/specification/2025-11-25/basic/transports)
uses initialize/session headers, GET streams, optional DELETE and resumable
SSE. Those mechanisms cannot be silently mixed into a modern request.

`mcp_http_metadata.cpp` now projects standard headers from actual native request
bytes and validates tool parameter header declarations. Values preserve exact
property paths, Unicode/control/whitespace encoding, boolean spelling and safe
integer precision without a floating-point round trip. Null/absent values omit
headers. Invalid declarations throw before projection; the HTTP owner catches
declaration rejection per tool and omits that tool from discovery.
Existing stdio discovery remains unchanged. Byte/header-count limits are xMind
implementation limits. This component is linked into the actual MCP wire target
and its expanded native wire contract passes. Full protocol coverage remains
unfinished.

The native Windows POST implementation reuses the existing async WinHTTP
transport's certificate, redirect, credential-wiping and deadline machinery.
It adds a typed MCP boundary: actual request/schema-derived headers, explicit
older session input, JSON/SSE response selection, response status/session/auth
challenge metadata, empty 202 acknowledgement enforcement and a conservative
possibly-sent callback immediately before the send API. Error responses remain
backend-private protocol input. No retry, redirect forwarding or approval
authority is added. Its existing real-socket transport contract now requires
exact request counts, UTF-8/raw-decimal preservation, both response media,
older session metadata, 400 RPC errors, 401 challenges, empty/invalid 202,
cancellation, deadlines, invalid metadata and certificate rejection. The peer
is synthetic; the separate effect contract establishes the scoped official SDK
interoperability below. These real-socket assertions pass natively.
The source now also has `McpHttpClient`, implementing `McpToolClient` with the
same private dispatch boundary as stdio. Its incremental JSON/SSE decoder uses
the strict existing JSON-RPC envelope codec and retains literal result tokens.
It negotiates modern discovery and structured older fallback, caches immutable
tool bindings, filters invalid header declarations and projects actual calls
before the send boundary. Changed cached bindings retire the owner. Interrupted
calls preserve observed response bytes and possibly-sent attribution; neither
stream IDs nor retry fields trigger request replay. Native compilation and the
configured owner/effect assertions pass. The configuration/factory select both bindings in agent
execution, idle context preparation, direct graph tools and admin discovery.
HTTP uses a backend-owned endpoint and optional encrypted server-scope bearer
reference. Its credential purpose binds exact endpoint, configuration identity
and authorization target. Endpoint changes require newly bound credentials.
Native authority binds transport/destination/reference/credential revision.
Ambiguous process/HTTP fields, arbitrary headers, plaintext values, unsupported
scopes, remote plaintext URLs, embedded credentials, fragments and controls are
rejected. Admin catalogue reflection checks retain private wiping credential
copies; clients/models receive no token. Existing revision/retirement and SQLite
ownership rules apply. Configuration, authority and actual HTTP effect assertions
exercise these paths and pass natively. Full public agent/graph HTTP-MCP workflows
and installed/rendered connection controls remain required.

The existing native effect contract now adds a pinned official SDK HTTP host
(`@modelcontextprotocol/server` 2.3.1 / Zod 4.2.0). It requires actual remote
JSON/SSE writes after durable native approval, denial without dispatch, duplicate
operation rejection, lost-reply uncertainty and xlang3 SQLite restart records.
The host independently compares peer disk bytes and dispatch counts. Existing
stdio effects remain in the same contract; the exact contract count is unchanged.
The actual native contract passes through persisted configuration, encrypted
credential resolution and the production factory. The independent SDK host
verifies peer disk bytes and exact tool-call counts. This establishes neither
live model execution nor rendered UI acceptance.

OAuth, empty/non-JSON older discovery fallback, older server-request replies,
resumable GET/DELETE, notification subscription ownership and graphical connection
controls remain unfinished. The current owner explicitly fails unsupported
responses rather than admitting tool effects through another transport.

The following are xMind implementation decisions, not claims of delivered
protocol support:

- The native backend selects a trusted endpoint and immutable configuration
  revision. Views/models cannot supply an endpoint, raw header or token.
- The HTTP binding reuses native negotiation/correlation and the common
  approval registry. Protocol IDs/cursors belong to the MCP owner; UI SQLite
  event sequences remain separate.
- TLS uses OS verification. Redirects cannot forward credentials to another
  destination. Parsing, response sizes, session IDs and deadlines are bounded.
- Transport loss records uncertain effects when dispatch may have occurred.
  Older resumable observation cannot repeat a journalled tool POST. Modern
  stream closure cannot be treated as automatic resumable observation. UI
  disconnection detaches the viewer; it must not close runtime-owned MCP work.
- The factory selects and connects the actual transport before registry
  discovery. The configured native factory/approval/effect path is verified;
  hosted packaging and installed public workflows remain required separately.

## Native configuration

Trusted startup/offline-admin input can describe an HTTP server as:

```json
{"servers":[{"id":"remote-tools","transport":"http","endpoint":"https://mcp.example.test/mcp","credential":{"scope":"server","id":"remote-tools-key"}}]}
```

This is a documentation placeholder, not a tested service. Omit `credential`
only for an explicitly configured unauthenticated endpoint. The existing
`import-mcp` and `put-mcp-credential` admin commands use this registered
reference; the HTTP credential target is `BEARER`. Secret input remains a private
environment source, encrypted by the native backend and stored through xlang3
SQLite. It is never a JSON value, request argument, public event or command-line
secret. Import/provision while the backend is stopped; its runtime configuration
snapshot is immutable. No live profile or user configuration was changed here.

## OAuth authorization

The [modern MCP authorization specification](https://modelcontextprotocol.io/specification/2026-07-28/basic/authorization)
requires resource and authorization-server discovery, PKCE capability checks
and resource parameters in authorization/token requests. HTTP session identity
does not substitute for authorization.

xMind will own authorization state and encrypted access/refresh credentials
behind embedded-xlang3 SQLite. Reference purposes must bind the server/config,
resource, issuer and account. Browser and VS Code views initiate the user flow
through native commands and receive public status; they do not retain refresh
tokens or replace effect approval.

`mcp_oauth.cpp` provides bounded native resource/authorization-server metadata
discovery and Bearer challenge parsing. It derives path-specific/root resource
URLs and OAuth/OpenID issuer candidates, accepts only HTTPS authorization
endpoints, verifies exact resource/issuer identities and requires advertised
`S256` and authorization-code support. Metadata is fetched without credentials;
only a 404 advances to the next candidate. Expected issuers are not normalized.
Quoted commas and separate `WWW-Authenticate` fields are parsed together, with
bounds and duplicate-parameter rejection. Ambiguous multiple Bearer challenges
are explicitly rejected. A native 401/403 retires the owner with a typed
authorization requirement; it cannot automatically replay a tool call.

Native acceptance covers metadata/challenge fixtures, a real socket 401 with
Basic and Bearer in separate fields, exact challenge/query retention and owner
retirement, pre-cancellation/deadline checks and OS rejection of an untrusted TLS
certificate. No positive HTTPS discovery against a trusted authorization server,
registration, callback/state/PKCE exchange, token storage/refresh/revocation or
user-facing login flow has been verified. Those remain required.

## Native authorization-owner component

The native library now has a single-caller `McpOAuthAuthorizationAttempt` for a
pre-registered public client whose selected issuer advertises `none` token
authentication, authorization-code support and `S256`. Windows cryptographic RNG
creates independent state/verifier values; BCrypt SHA-256 and unpadded base64url
create PKCE challenges. Public authorization URLs include the registered client,
redirect, resource and selected scopes, with no code verifier. Challenged scopes
are not restricted to the resource's advertised list.

Callbacks bind the exact redirect, state and decoded issuer before accepting an
authorization code or error. Duplicate fields, malformed encoding, expiry,
mismatched issuer, missing required issuer and replay retire the attempt. A validated
denial exposes an exact allowlisted reason, never descriptions or error URIs.
The owner consumes its authority before token POST; cancellation, failures and
repeated exchange cannot automatically replay the grant.

The production code-grant encoder returns a private wiping form containing the
resource, client, redirect, authorization code and verifier. Native form POST
uses no provider credentials or caller-selected headers, bounded wiping response
storage and OS TLS/redirect/cancellation policy. OAuth error bodies are not
provider diagnostics. Private token parsing checks token type, token bounds,
integer expiry and returned scopes. Application code owns input copies; owned
buffer cleanup is not a claim that every allocator or OS transport copy is erased.

The complete native build and **110/110** local gate pass in **376.23 seconds**,
with **2439** unchanged mapped inputs, the pinned xlang3 runtime and its CPython
bridge disabled. Independent socket assertions require exact production grant
fields and an independently computed RFC PKCE challenge. Raw build/test output,
contract inventory, source hashes and actual binary hashes are retained in
[authorization component evidence](evidence/native-oauth-authorization-local.json).
Fixtures exercise component semantics and loopback byte transport;
native authorization exchange itself still requires HTTPS and rejects the
untrusted fixture certificate. No trusted HTTPS user login, callback listener,
encrypted authorization/token persistence, registration selection, refresh,
revocation or browser/VS Code login controls are delivered by this component.
This owner supports pre-registered public clients with token authentication
`none`. Redirect query strings and authorization attempts longer than ten minutes
are rejected by the current component. Hosted packaging and installed/rendered
acceptance of this source remain separate requirements.

## Native loopback callback component

`McpOAuthLoopbackCallback` binds exclusively to IPv4 `127.0.0.1` before the
backend creates its authorization attempt. It supports an ephemeral port or an
explicit registered port and exposes its exact redirect URI. The caller must
register/use that URI with the selected authorization server. The receiver is
single-use and validates the attempt's exact redirect before listening. Its
deadline is bounded by the authorization owner's expiry.

The GET callback passes the original query bytes to the native state/issuer/code
owner. Wrong Host, Origin/Authorization headers, other paths/methods and oversized
targets cannot admit a code. Duplicate state, missing required issuer, malformed
target and denial retire the exchange as specified by the owner. Fixed callback
pages contain no reflected query fields, outbound links or scripts, and use
no-store/no-referrer/CSP headers. HTTP parsing has private temporary copies;
this is not a promise to erase every library or OS buffer. The receiver performs
neither token exchange nor tool dispatch. This remains a native library component;
the authenticated product setup routes and view controls do not yet call it.

The full native build and **111/111** local contracts pass in **378.16 seconds**,
with **2443** unchanged mapped inputs and the pinned bridge-disabled xlang3 SDK.
The first callback-only build failed because the protocol-error declaration
include was missing; the original failure is retained. That include was repaired
before the full build/gate. The original fixture checked socket closure after
process exit. A test-only follow-up then strengthened both fixture files: all
**12** callback cases now check closure while the native process and receiver
remain alive awaiting an independent peer acknowledgement. The rebuilt callback
contract passes in **0.99 seconds**. All production inputs and recorded production
binaries remain unchanged; the complete 111-test gate was not repeated after
that two-file fixture change. The exact scopes/hashes/raw outputs are retained in
[callback evidence](evidence/native-oauth-callback-local.json).

Trusted HTTPS discovery/login/token exchange, encrypted grant persistence,
registration selection, refresh/revocation and client login controls remain
required. Current callback paths use bounded literal ASCII letters/digits,
slashes, underscores and hyphens. IPv6, HTTPS and non-query response-mode
callback receivers are separate work.

## Encrypted OAuth grant component

HTTP MCP configuration now accepts one `oauth` reference containing `scope`,
`id`, `issuer` and `client_id`, as an alternative to the static `credential`
reference. The implemented scope is `server`. Resource and issuer must use HTTPS;
the selected issuer has no query. Unknown fields and mixed authentication
methods are rejected. Public configuration contains no access/refresh tokens.

`McpOAuthCredentialStore` stores access and optional refresh tokens, granted
scopes, receipt timestamp, optional expiry and the validated HTTPS token endpoint
in one bounded encrypted record. Native purpose binding includes exact server,
resource, issuer and public client identities; DPAPI context additionally binds
the credential scope/ID and record revision. Binary private encoding avoids
converting tokens into public JSON. Owned encoding/token buffers are wiped on
release; this is not an all-allocator/OS-copy erasure guarantee.

The repository now resolves metadata and protected bytes from one SQLite SELECT
through xlang3, yielding one exact credential revision with the plaintext.
Refresh tokens retain valid opaque spaces/punctuation from the
[OAuth refresh-token syntax](https://www.rfc-editor.org/rfc/rfc6749.html#appendix-A.17).
They are never resource-server header values. Compare-and-swap replacement
atomically rotates the complete grant. Removal
permanently retires that credential identity; new authorization requires a new
reference. Credential administration is available for disabled connectors,
while the factory still refuses to connect them. Local removal is not an OAuth
revocation request and does not yet coordinate already-running owners.

The actual MCP factory resolves the encrypted grant, rejects missing/expired
grants or a clock rollback and passes only the access token to the HTTP owner.
Both tokens feed private catalogue-reflection checks. The owner checks known
expiry before each request, including discovery and later tool requests. Missing
expiry is retained as unknown rather than inventing a lifetime. Private agent
authority binds OAuth references and the actual credential revision, so rotation
changes the admission digest. Automatic refresh/retry is not implemented.

Focused native grant storage, authority and real-socket validity contracts pass
**3/3** in **12.26 seconds**. The socket peer requires exactly one successful
discovery and zero later requests after expiry. The first focused build had a
test-fixture lambda syntax error; its original output is retained. The final
complete native build/gate, including missing-grant, refresh-token spaces and
disabled-cleanup assertions, passes **112/112** in **384.25 seconds**, with
**2446** unchanged mapped inputs and the pinned bridge-disabled xlang3 SDK.
Raw build/test output, exact test inventory, source and binary hashes are in
[grant evidence](evidence/native-oauth-grants-local.json). Client source stayed
unchanged; the prior 244/54 synthetic view suites were not rerun for this
native checkpoint. There is no trusted HTTPS OAuth login, authenticated product setup
flow, refresh/rotation/revocation request or view login acceptance yet.

## Acceptance still required

The configured HTTP/terminal-feed package at `7007010` has separately passed the
complete hosted **110/110** gate in **447.12 seconds**, **244/54** client suites
and downloaded bundle/VSIX hash verification. It predates the newer OAuth
authorization owner and callback listener. Its **1860** runtime files and **29**
source-bundle files match recorded manifests; packaged view sources match the
commit. This is no installed/rendered acceptance of the current implementation.
[Original hosted evidence](evidence/native-mcp-http-hosted-7007010.json).

- Verify hosted packaging, installed public agent/graph HTTP-MCP workflows and
  rendered approval UI against the current source.
- Expand resources/prompts, server-request replies and notification/subscription
  ownership. Verify older initialization/session expiry, resumable GET and
  DELETE separately; empty/non-JSON discovery fallback remains unfinished.
- Verify concurrent independent HTTP owners and credential/destination isolation.
- Exercise positive trusted HTTPS OAuth discovery, registration, callback/state,
  PKCE exchange, resource/issuer/account binding, token storage/rotation and
  user-facing login controls without exposing credentials or replaying effects.

UI SSE, a source-only interface, provider HTTP helpers and successful stdio peers
do not establish those remaining HTTP/OAuth behaviors.
