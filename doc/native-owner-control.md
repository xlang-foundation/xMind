# Native owner control transport

The newer [replacement protocol](native-owner-replacement.md) enables qualified
production-server retirement and activation and passed the complete 96-contract
local gate. The 95-contract scope below records the earlier optional-controller
checkpoint, which reports `retirement_supported: false` without an owner-token
binding. Installed legacy owners remain separate and are not upgraded by this
component validation.

The C++ `BackendOwnerControl` joins the durable persistence barrier to the
actual execution workspace, runtime health/idleness and verified loaded
package. It can be attached to `HttpServer` only when it covers that exact
persistence, executor and graph boundary. Unknown execution implementations
default to non-idle. It uses a shared native admission lock for normal commands
and an exclusive attempt for owner commands; quiescence rejects in-flight
admissions instead of waiting for them or cancelling work.

The optional authenticated HTTP boundary implements:

| Route | Preconditions |
| --- | --- |
| `GET /v1/backend/owner` | Native owner bearer authentication; observe generation, revision, phase and receipt. |
| `POST /v1/backend/owner/quiesce` | Exact generation/revision and workspace ID/authority; healthy, idle runtime and idle persisted owners. |
| `POST /v1/backend/owner/resume` | Same live generation and exact receipt/revision/workspace authority. |

Scoped browser view credentials cannot invoke these routes. They are not model
tools. Queries and unknown/duplicate body fields are rejected. The response
explicitly reports `retirement_supported: false`.
The newer backend-only [retirement request](native-owner-retirement.md) applies
the same native checks. HTTP status exposes `retirement_requested`, but no
public retirement/shutdown route is enabled.

Registered mutating HTTP handlers retain their shared admission guard through
execution. Context status GET is guarded too because it can start counting or
indexing maintenance. The raw incoming A2A handler uses the same guard.
Persistence checks its durable fence on every queued mutation independently.
Ordinary read observations remain available. Health does not advertise agent
execution while the attached owner is quiesced.

The focused Windows contract passed through the actual C++ HTTP server,
execution platform and embedded-xlang3 SQLite repository. It checks scoped-view
denial, workspace/receipt preconditions, busy persisted ownership, context/A2A
rejection while quiesced, preserved sessions and concurrent HTTP mutations.
Deployment metadata and unused package members are fixtures; the running C++
test image is qualified by actual OS identity. There are no provider calls or
file effects. The first fixture's inert SQLite DLL incorrectly shadowed its
allowed module; the corrected fixture uses the actual xlang3 JSON/SQLite modules.
No interpreter fallback or native SDK workaround was used.

The complete native gate passed **95 contracts in 190.08 seconds**, with all
622 input hashes unchanged. The earlier fixture failures remain recorded.
[Exact verification and scope](evidence/native-owner-control-http-local.json).

The production `xmind_server` does not attach this controller yet. Actual shutdown,
replacement admission, receipt-controlled startup and the explicit legacy
operator migration still need implementation before enabling it there. The
normal VS Code `TestProj` backend is not upgraded by this work and writing in
that installed sidebar remains unverified.
