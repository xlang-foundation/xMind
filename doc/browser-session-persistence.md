# Durable local browser access sessions

The live preview now uses the tested durable native access-session service.
Its HttpOnly cookie survives refresh and access-adapter/native backend restarts
at the same addresses within the eight-hour native expiry. Provider keys remain
in the backend encrypted credential repository. The earlier adapter held its
session map in memory and required login after an adapter restart.

The `ViewSessions` C++ service implements durable access sessions for that
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
token. The adapter retains the credential in an HttpOnly cookie and forwards it
with its bound origin; native verification runs on each request.

The existing native persistence contract now includes issuance, secret tamper,
origin mismatch, metadata disclosure checks, real repository reopen, master
rotation, durable revocation and expiry. It also injects actual SQLite trigger
failures during metadata publication and credential deletion: unpublished
candidates must be pruned on the next enrollment, while failed revocation must
preserve the previous credential transactionally. The test requires the
32-session capacity limit to reject before adding another credential, then
checks that revocation releases capacity. These source tests are not yet
compiled or executed: the local build guard deferred while sibling xlang3
benchmarks were running. The live browser continues using its existing
in-memory sessions; restart persistence is not yet available to users.

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

A separate local adapter contract passed against a labelled synthetic native
access peer. It checks real HTTP forwarding, adapter restart at the same origin,
bound credential headers, disconnect/revocation, duplicate-cookie and origin
rejection, failed replacement login, malformed enrollment replies and backend
unavailability. The adapter never sends the master token outside explicit
enrollment or in JSON. All 12 browser tests pass locally. This peer verifies
adapter behavior only; the C++/xlang3 restart and SQL fault tests remain pending.

The pending statements above describe earlier source checkpoints. Revision
`6263630` has now passed its full hosted gate: 52 native contracts, 70 extension
tests, 12 browser tests and packaging. The actual native/browser contract
restarted both components and restored the retained cookie, graph state and
history without replay. [Hosted provenance](evidence/native-durable-view-sessions-hosted-provenance.json)
and [restart contract output](evidence/native-durable-view-sessions-browser-contract.log)
record the exact scope.

The live browser was upgraded to that exact native binary and adapter source,
then signed in once to replace its earlier in-memory cookie. Initial migration
sign-in attempts returned adapter HTTP 502; direct native enrollment and later
retries succeeded. The cause was not established. Temporary secret-free
diagnostics were removed, restoring the exact tested adapter before final
checks. The access adapter and then native backend were restarted at their
existing addresses. Browser refresh reused the cookie with no login prompt,
showing the selected `gpt-5.6-sol` Responses model and completed command history.
All six conversations, ten runs and 24 history records, plus the saved provider
configuration, survived without inference replay.
[Live restart evidence](evidence/browser-durable-session-live-restarts.json)
records sanitized results and the unresolved initial enrollment issue. This
does not establish remote/team authentication or eliminate that known issue.

Newer source returns `max_age_seconds` from the native lifetime policy alongside
the absolute expiry. The adapter validates a duration of 1–28,800 seconds and
uses that value for its cookie. Native validation continues to enforce the
stored absolute expiry on every request. Enrollment no longer compares the
native expiry to the Node process's clock at an exact eight-hour boundary.
Labelled adapter tests pass with the peer clock five seconds ahead or behind,
and reject missing/excessive durations without setting a cookie. A known
protocol-error code explains that mismatched server/adapter versions must be
updated together, preserves an existing connection and clears the token input.

All 14 browser adapter/DOM tests pass locally. Native persistence tests now
check durations for both eight-hour and one-second policies, and the actual
native/browser contract checks the public duration and revocation. Compiled
execution subsequently passed the exact hosted checkpoint below; the local
benchmark guard still defers development-machine compilation.
This removes a clock-precision dependency demonstrated by fixtures, but the
earlier live 502 cause remains unproven. The live preview still uses the exact
tested `4203b84` pair; this changed contract must roll out as a matched pair.
Remote/team access is still outside this local adapter.

Hosted checkpoint `e9f562aa0bb412d0f871fee7bc0db87d2b469dc1` passed 52 native,
72 extension and 14 browser contracts in
[run 37708019275](https://github.com/xlang-foundation/xMind/actions/runs/37708019275).
The exact successful job log and evidence artifact were inspected, including
the actual native duration/revocation and browser/native restart contract.
[Provenance and limits](evidence/native-view-session-duration-hosted-provenance.json),
[projected results](evidence/native-view-session-duration-hosted-summary.log).
The clock-skew and invalid-duration checks remain explicitly labelled adapter
fixtures. This gate verifies the duration contract; it neither diagnoses the
earlier live enrollment 502 nor claims that the newer pair is installed.
