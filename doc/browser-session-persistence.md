# Durable local browser access sessions

The current browser adapter keeps its eight-hour sessions in memory. A UI
refresh and a native restart at the same origin preserve access while that
adapter remains alive; restarting the adapter currently requires login again.
Provider keys remain in the native encrypted credential repository.

The new `ViewSessions` C++ service prepares durable access sessions for that
remaining restart case. Windows BCrypt generates independent 256-bit identifiers
and secrets. The secret is encrypted by the existing credential repository;
origin, expiry and the SHA-256 binding to the master authority are public
metadata. All persistence calls use the existing embedded xlang3 service.
The service does not store the master token. Cookie credentials have the form
`identifier.secret`, and validation compares the secret without early exits.
Origin mismatch, expiry, malformed credentials and authority rotation reject
access. Revocation deletes the encrypted credential, so it survives reopening.
Issuance prunes inactive credential records and limits active sessions to 32.
Expiry does not extend when a view reconnects.

This is a local access service, not company/team authentication. Its origin
policy currently accepts canonical `http://127.0.0.1:PORT` origins only. Remote
deployments still require their own authenticated access adapter and transport.
Issuance must be called only after master-token authentication. A view credential
must never be permitted to issue additional credentials or receive the master
token. The planned adapter will retain the credential in an HttpOnly cookie and
forward it with its bound origin; native verification must run on each request.

The existing native persistence contract now includes issuance, secret tamper,
origin mismatch, metadata disclosure checks, real repository reopen, master
rotation, durable revocation and expiry. This source is not yet compiled or
executed: the local native build guard deferred while sibling xlang3 benchmarks
were running. HTTP and browser adapter integration are still pending. The live
browser therefore continues using its existing in-memory access sessions;
restart persistence is not yet available to users.

HTTP and adapter integration is now present in source. Master authentication can
issue a credential through `POST /v1/view-sessions`. The adapter uses an HttpOnly
cookie to forward `Authorization: View ...` with its canonical origin. Native
authentication checks every request and limits this credential to the existing
view routes plus current-session/revocation endpoints. It cannot create more
sessions or call A2A. No master token or login map remains in adapter state.
Disconnect clears the cookie and revokes its native credential; closing the
adapter leaves the bounded native session available for reconnecting.

The actual native/browser contract now restarts the disposable adapter at the
same port, authenticates the retained cookie, then restarts the native backend
and checks completed graph state and history without replay. It also rejects
cross-origin credentials, duplicate cookies and session-issuance/A2A privilege
escalation. JavaScript syntax checks and all 11 browser adapter/DOM tests pass
locally. The expanded native test has not run: compilation was deferred again
by the active benchmark guard. Existing live previews keep their already-loaded
adapter and tested native binary; updating requires the matching verified pair.
The native persistence tests, integration test and live restart acceptance are
still pending, so this document does not claim the feature is ready to use.
