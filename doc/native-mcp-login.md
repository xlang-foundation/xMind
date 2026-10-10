# Native MCP login service and client controls

The shared native service now also has [native console commands](native-mcp-console.md).
The registered-callback source passes the complete **115/115** native gate in
**388.83 seconds**, with **2460** unchanged inputs. Client inputs are unchanged
from their prior complete **257 extension / 55 browser** suites; those suites
were not rerun for this source. Actual console-to-service acceptance covers
metadata, admission, observation and terminal cancellation with an untrusted TLS
peer. Trusted HTTPS positive login and actual browser launch remain unverified.
[Exact current checkpoint](evidence/native-oauth-registered-local.json).

The C++ backend owns asynchronous OAuth discovery, callback receipt, code
exchange and encrypted grant publication. Browser and VS Code Settings expose
sign-in controls for backend-configured OAuth MCP servers. A view sends server
identity and observed configuration/credential revisions, never an endpoint,
issuer, client registration or token. Closing a view detaches observation.

## Authenticated commands

| Command | Route | Result |
| --- | --- | --- |
| Server status | `GET /v1/mcp/authorization/servers` | Public server/revision/expiry metadata |
| Start login | `POST /v1/mcp/authorization/attempts` | Accepted attempt; asynchronous native execution |
| Observe login | `GET /v1/mcp/authorization/attempts/<id>` | Actual current native phase or terminal outcome |
| Cancel login | `POST /v1/mcp/authorization/attempts/<id>/cancel` | Requests cancellation; retains any already committed outcome |

Start accepts exactly `server_id`, `expected_config_revision`,
`expected_credential_revision` and `request_id`. Cancel accepts an empty JSON
object. All routes reject query targets, including an empty query. Native owner
credentials or an origin-bound native View credential authorize the supported
methods. The browser access adapter retains its same-origin and HttpOnly cookie
boundary; raw configuration, credential and manual completion routes stay absent.

Repeated start with the same request ID and original context observes the same
attempt. Changed context conflicts. A configured server has at most one active
attempt; the service permits four active attempts, retains 64 observations and
bounded retired request IDs. It rejects a new login while an existing grant is
usable. Rotation/revocation of running owners is separate unfinished work.

The production owner probes the real MCP HTTP endpoint without a bearer,
validates protected-resource/issuer metadata, creates PKCE and state, binds an
exclusive IPv4 loopback listener and exchanges a valid callback over
native HTTPS. It checks the persisted configuration again before saving the
complete encrypted grant with its expected credential revision. Publication and
cancellation share the service lock; persistence also fences native owner
generation/quiescence. No view can set a successful state or submit a token.

## Browser and VS Code Settings

Settings displays each configured server's actual status. **Sign in** admits a
native attempt; **Open sign-in page** opens only an observed HTTPS authorization
URL in the callback-wait phase. **Cancel sign-in** requests native cancellation.
The view validates exact public response fields and server/request/revision
ownership. It never receives access or refresh tokens.

Saved observations contain only the request/server IDs and original revisions.
VS Code scopes them to its selected backend/workspace; the browser keeps them
with its existing view selection. Reopening observes by GET and never replays a
start. A lost start reply is observed using the saved request ID. Missing attempts
after a backend restart are discarded, requiring an explicit new login. Backend
attempts themselves are process-owned and are not resumed after process exit.

## Verification and unfinished delivery

The independent Node peer runs the actual compiled C++ service with embedded
xlang3 SQLite, an existing encrypted synthetic grant, a self-signed HTTPS server,
native HTTP and origin-bound View sessions. It requires actual TLS contact and
zero HTTP requests to that untrusted server, actual asynchronous failure, no new
grant publication, identity/CAS rejection, retained terminal cancellation,
forbidden input rejection and production browser adapter/client/controller
commands against the native service. Synthetic client, DOM and VS Code API
fixtures separately verify links, owner binding, lost-reply observation and view
disposal. These scopes do not establish a successful OAuth login.

The current flow uses a backend-configured, pre-registered **public** client ID.
The candidate adds [registered callback path/port settings](native-mcp-registered-callback.md)
with exact binding and no occupied-port fallback. Configuration/grant, receiver
and authority contracts pass locally; successful registered native login and
service-level occupied-port rejection still require hosted acceptance.
Registration selection, CIMD/DCR, positive trusted
HTTPS login, automatic refresh, revocation, coordination of running MCP owners,
full graphical connector configuration remain unfinished.
Fresh packaged/installed/rendered current UI acceptance is also required.

The separately prepared [trusted HTTPS acceptance](native-mcp-trusted-acceptance.md)
requires actual successful native login on an isolated Windows runner, encrypted
grant reopen and production factory bearer use. Local peer verification and
compilation do not establish that positive native result.

The complete checkpoint gate and its original failures are recorded in
[local evidence](evidence/native-oauth-service-local.json). The protocol and
transport boundary remains in [native MCP HTTP](native-mcp-http.md).
