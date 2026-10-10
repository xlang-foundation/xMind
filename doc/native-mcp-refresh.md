# Native MCP OAuth refresh

The native library now contains a single-caller `McpOAuthRefreshAttempt` and
private form/response helpers. This is an exchange primitive; automatic refresh,
durable refresh ownership/publication and CLI/view refresh controls are not
implemented. The production MCP factory still rejects a known expired grant.
Adding a request encoder does not make expired production connections work.

## Implemented exchange behavior

The constructor binds the selected resource and issuer, public client ID,
exact previously stored token endpoint and granted scopes. Metadata must
advertise the selected issuer and public-client `none` authentication. HTTPS
validation remains enabled. Changed endpoints, invalid token syntax/scopes and
unbounded lifetimes are rejected before exchange.

The private form contains `grant_type=refresh_token`, the public client ID,
resource, opaque refresh token and the previous scopes when nonempty. It carries
no access bearer, local-owner credential, authorization code, verifier or
callback. Native transport does not follow redirects or send ambient credentials.

A response cannot increase the previously known scopes. A new refresh token
replaces the previous token; an omitted refresh token preserves its exact bytes.
Malformed/duplicate JSON, invalid token/expiry fields and OAuth error responses
fail. Private native buffers are wiped through the existing secret/JSON owners;
transport error bodies are not exposed as provider diagnostics.

Each invocation consumes its owner, including success, cancellation, timeout,
HTTP, transport or parsing failure. It has no retry or tool-call replay path.
This in-memory property does not establish crash/restart non-replay. The caller
must own a durable claim before invoking it.

These rules follow [OAuth refresh semantics](https://www.rfc-editor.org/rfc/rfc6749#section-6)
and [resource indicators](https://www.rfc-editor.org/rfc/rfc8707#section-2.2).

## Verification boundary

Focused compilation and two contracts pass. An independent Node decoder checks
actual native form output, including UTF-8, spaces and form punctuation. Native
fixtures check rotation/retention, scope/metadata/parser rejection, move and
single-use cancellation/deadline behavior. A real self-signed HTTPS peer observes
native TLS contact and zero HTTP requests. The independent authority separately
exercises seven refresh modes over Node HTTPS with explicit CA verification.
The first complete rebuilt gate passed 115 of 116 and failed managed profile
startup at its unchanged 60-second fixture deadline, in 376.95 seconds. Original
outputs and the retained fixture are preserved. Repeating the original focused
contract failed at the same unchanged deadline. A separate diagnostic with a
longer watchdog completed and measured the first cold start at 57,196 ms, with
subsequent attachments under three seconds. That modified diagnostic is not
acceptance of the original contract. Directory preparation has been changed to
prepare and pin each destination directory once, using extended Windows paths
while preserving private ACL and alias checks. Its rebuilt original focused
contract passed in 75.40 seconds with the unchanged per-command watchdog. This
does not isolate the cause of the earlier timing failures. The following complete
rebuilt **116/116** local gate passes in **382.06 seconds**, with **2462** unchanged
frozen inputs and the original managed-profile contract passing in that gate.
Client inputs are unchanged from the prior complete **257 extension / 55 browser**
result; those suites were not rerun.
[Complete local gate evidence](evidence/native-oauth-refresh-local.json).
[Original full-gate failure](evidence/native-oauth-refresh-first-failed.json).
[Original repeat and qualified diagnostic](evidence/native-oauth-refresh-profile-diagnostics.json).
[Rebuilt original focused startup acceptance](evidence/native-oauth-refresh-startup-local.json).

The mandatory hosted gate now requires 117 contracts. Its owned test root also
allows the actual native primitive to exercise rotation, retention, scope
expansion, duplicate JSON, HTTP failure, lost reply and redirect rejection. It
requires exactly one request per owner and zero forwarded requests. These are
synthetic protocol inputs, separate from the actual login service/grant tests.
Hosted native refresh remains unverified; even a pass will not prove durable
refresh publication, automatic renewal, real authority or installed acceptance.
The local complete gate identifies native checkpoint `6afaaf7`. A subsequent
test-only provisioning repair changes the hosted certificate store and cleanup
assertions; it does not change compiled native product code. Its successful
hosted execution is still required.

## Required production integration

1. Bind the current configuration/credential revisions, exact encrypted grant
   snapshot, endpoint and authority before preparing a refresh.
2. Persist an exclusive, revision-bound claim under the current backend
   generation; fence competing rotation/deletion before any network request.
3. Record dispatch before exchange. On a possibly consumed request or lost
   response, retain uncertainty and prohibit automatic replay after restart.
4. Publish the complete encrypted grant and committed receipt atomically through
   xlang3 SQLite. A grant write followed by a separate receipt write is inadequate.
5. Coordinate existing MCP owners and admitted authority without rebinding an
   in-flight effect or replaying its tool call.
6. Expose only actual metadata/receipts through authenticated backend commands,
   CLI and thin views, including explicit recovery when a fresh login is required.

### Repository and owner boundaries

The current `McpOAuthCredentialStore::save` encrypts and writes the complete grant
through `PersistenceService::put_credential`; there is no refresh receipt in that
transaction. `put_information` and `put_credential` are separate commits. Calling
them consecutively cannot publish a refresh atomically. Refresh publication must
be a dedicated repository operation on the existing persistence thread, with the
complete encrypted replacement and receipt committed in one transaction.

The claim must identify server/configuration revision, credential scope/ID,
purpose and revision, token endpoint, request identity and actual backend owner
generation. Its states need to distinguish preparation, recorded dispatch,
committed publication and an uncertain exchange. A repeated request observes its
existing receipt; it never starts another exchange. Recovery may retire a
provably undispatched preparation, but dispatched or uncertain revisions must
remain unusable for another refresh until a fresh authorization replaces them.

Every mutation of the claimed credential must participate in that exclusion,
including generic credential replacement/deletion and login publication. Checking
only the refresh service's in-memory map leaves other writers able to invalidate
the authority after dispatch. Configuration checks and publication must also
share the repository transaction rather than a configuration read followed by a
later unconditional credential write. Existing persistence mutations already
verify the live backend generation and quiescence before execution; refresh
operations must preserve those checks.

The production factory currently loads the encrypted snapshot and refuses an
expired grant before connecting. Successful renewal will change credential
revision and therefore admitted MCP authority. New admission can load the
published revision; an existing tool effect must retain its admitted authority
and cannot silently acquire the replacement or replay after a failed exchange.
Owners waiting on renewal must observe its durable receipt, cancellation and
deadline, rather than each issuing the same refresh token independently.

Registration/CIMD/DCR, revocation, full connector configuration and fresh
installed/rendered acceptance remain required. [Login service](native-mcp-login.md),
[trusted execution boundary](native-mcp-trusted-acceptance.md).
