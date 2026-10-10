# Native MCP grant renewal

The C++ OAuth service owns manual renewal of an existing encrypted HTTP MCP
grant. The console, browser and VS Code submit the same authenticated command;
they never receive a refresh token or call the authority themselves.

## Commands and ownership

`xmind mcp-refresh SERVER` (or `/mcp-refresh SERVER` in chat) obtains the current
configuration and grant revisions from the backend, generates a request identity
and publishes it before sending. Use `mcp-login-status REQUEST_ID` or
`mcp-login-watch REQUEST_ID` to observe that identity after a lost reply. The
existing cancellation command also applies. Closing a console only detaches.

Browser and VS Code Settings offer **Renew grant** for an enabled OAuth server
with an existing grant. They persist the old revision and request identity before
admission, then observe through the existing attempt routes. Reload and detach
do not repeat the renewal POST. The backend also requires a stored refresh token;
an access-only grant cannot be renewed.

The native route is `POST /v1/mcp/authorization/renewals` with exactly
`server_id`, `expected_config_revision`, `expected_credential_revision` and
`request_id`. The credential revision must be positive. The reply uses the
existing attempt metadata whitelist. Status and cancellation use
`/v1/mcp/authorization/attempts/REQUEST_ID` and its `/cancel` endpoint. No client
endpoint, issuer, scope, token or state override is accepted.

## Exchange and durable publication

Admission claims one private, generation-bound grant snapshot through embedded
xlang3 SQLite. The coordinator probes the configured resource anonymously and
rediscovers the selected issuer. The selected token endpoint must match the
stored grant exactly. The previous scopes, client ID, resource and refresh token
form the single-use exchange. Dispatch is journaled before network submission.

A successful response restricts scopes and retains the old refresh token when
the authority omits a replacement. Complete encrypted grant publication and the
committed receipt share one transaction. Before changing a receipt after an
error, the coordinator rereads it: an actual committed result wins even if its
acknowledgement was lost. Duplicate identities return receipts without obtaining
another private snapshot or dispatching again.

Failure before dispatch cancels the prepared claim and preserves the old grant.
After dispatch, protocol failure, lost reply, cancellation or failed publication
settles as **uncertain**, without retry. The public reason is `refresh_uncertain`.
An unresolved durable recovery failure is `refresh_recovery_required`. An
uncertain credential remains fenced against renewal and generic replacement or
deletion; explicit recovery is still required product work. A new backend
generation retires prepared claims as cancelled and dispatched claims as
uncertain. Receipt observation remains available after restart.

## Execution boundary

Manual renewal currently requires idle backend execution and maintenance. New
root runs, incoming messages, graphs, idle context leases, input counts and
compaction admission refuse a prepared/dispatched renewal. Read-only inbound
message replay remains available. Renewal cannot rebind an already admitted
tool effect. Separate servers may renew concurrently within the bounded OAuth
worker capacity; a credential has only one dispatch owner.

## Verification boundary

The local negative contract runs the actual service, HTTP routes, xlang3 SQLite,
native console and browser adapter/controller against an untrusted synthetic
HTTPS peer. It requires zero HTTP dispatch and encrypted old-grant/receipt
reopen. Separate console and client/DOM fixtures test identity, revision,
lost-reply observation, private-field rejection and renewal controls.

The trusted synthetic authority contract is mandatory on the isolated hosted
Windows runner. It now includes actual integrated renewal for rotation,
retention, scope expansion, malformed JSON, HTTP failure, lost reply, redirect,
cancellation after dispatch and publication rollback. Each owner must send
exactly one refresh request; duplicate and restart observations send none.
Compiling this contract or testing its independent peer locally is not proof
that the actual native trusted exchange passed. Check the exact checkpoint's
evidence before claiming that acceptance.

Automatic expiry renewal, mid-run authority coordination, explicit uncertain
grant recovery, receipt archival/retention, real-account OAuth and current
installed/rendered client acceptance remain unfinished.
