# Native retirement request

`PersistenceService::request_backend_retirement(receipt)` consumes an exact
quiescence receipt while retaining the closed admission fence. It verifies the
actual database lease, live generation, receipt ID, revision and idle persisted
owners in one SQLite transaction through embedded xlang3. A successful request
advances the revision and persists `retirement_requested: true`.

The old generation cannot resume using either the consumed receipt or the
updated retirement record. Repeated retirement requests fail rather than
replaying the command. Status and saved-session observations remain available;
normal queued mutations remain blocked. A publication or commit failure retains
the prior resumable quiescence state.

Active and resumable-quiescence records retain their canonical version 1 format.
Only a retirement request uses version 2. Unknown fields, noncanonical state,
version 2 without true retirement, and retirement on an active record are rejected.

`BackendOwnerControl::request_retirement` applies the same native workspace,
runtime health/idleness, qualified loaded-image and exclusive admission checks
as the other owner controls. It is a backend C++ method. The optional HTTP
status exposes the actual retirement flag, but still reports
`retirement_supported: false`: no public retirement/shutdown command is enabled.

Persistence startup checks closed or malformed owner state under its actual
lease before schema migrations. An ordinary restart must not create newer
tables, change `user_version`, recover work or replace the saved owner record
when the owner cannot be adopted. This check runs on the persistence worker;
views never inspect or copy SQLite.

The contract additions use real native persistence, deferred foreign-key commit
failures, a concurrent resume/retirement race and disposable schema 12 fixtures
missing schema 13 skill tables. The optional HTTP fixture checks the native
controller request, observable closed state and rejected resume/mutation/A2A
commands after it. Provider inputs and package metadata are synthetic fixtures.

All **95 local native contracts passed in 193.41 seconds**, with 622 source
inputs unchanged and the exact registered/passed names matched to the complete
manifest. The detailed CTest log was replaced by a metadata-only inspection
before preservation; the complete gate summary and frozen-source maps remain.
Both owner contracts then passed a supplemental run to preserve their details.
That publication error and the earlier hosted failure are retained in the
[verification record](evidence/native-owner-retirement-local.json).

This is a durable retirement **request**, not a process-exit acknowledgement.
Process shutdown and observed exit, target-package/workspace/authentication
binding, qualified replacement startup and explicit legacy migration remain
required by [the runtime handoff](native-runtime-handoff.md). The currently
installed VS Code backend is unchanged by this component and remains read-only.

The earlier hosted `c9335ca` gate failed its scoped file-approval test at the
five-second deadline. Three subsequent local reproductions passed. The test now
reports actual terminal run state, operation states, event kinds/error codes and
synthetic provider progress when the scoped proposal is absent. It preserves
the five-second deadline and does not retry the model or treat the earlier
failure as passed. This diagnostic change does not establish a root-cause fix.
