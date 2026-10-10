# Managed native owner upgrades

## Current installed-profile boundary

On 2026-10-10, the live `TestProj` managed profile was observed at runtime
`57da085`, process 34252, port 49164. Its authenticated provider catalogue
returned revision 0 with no profiles. That runtime implements the native
owner-retirement protocol, but not provider-YAML import. The current native
view adapter has no upgrade command and `connect_local_profile` keeps the
profile's recorded runtime generation, so selecting a newer extension bundle
alone cannot update this profile.

The current runtime imported all four provider identities from the shared YAML
into a separate real xlang3 SQLite profile without making a provider or model
request. See the
[exact evidence](evidence/provider-import-live-local-20261010.json).

The missing integration is a native-view-owned explicit upgrade that retains
the same SQLite profile and owner token, obtains and consumes the exact native
retirement receipt, starts the verified replacement on the same backend port,
publishes its PID/runtime/authority back into encrypted `profile.state`, and
activates before admitting new work. Because persisted browser-view
credentials live in the same SQLite database and bind to the still-running
view-host process, preserving that database and host identity also preserves
those browser sessions. The upgrade must recover an interrupted handoff
without replaying retirement or starting a competing owner. Current source does
not yet implement this path; historical adapter checks below are not evidence
of installed-profile upgrade acceptance.

Historical checkpoint: the JavaScript owner registry and editor upgrade/cancel
commands described below have been removed from the current source. Native
owner-control contracts remain available to operators; this document is not a
current editor command guide. The replacement
[native view adapter](native-view-adapter.md) is pending acceptance.

The command now also handles authenticated legacy owners through
[native stop and ticket adoption](native-legacy-stop.md), with explicit product
confirmation, source-package verification and native writer-fenced termination.
The 98/198/39 local gate passed; the installed TestProj preview has not yet been
upgraded. The modern receipt path described below remains in use for qualified
owners.

The thin extension now starts new managed owners with the verified runtime
manifest digest. It retains a complete generation in private storage outside
opened folders and the extension installation directory. An extension update
cannot overwrite the running generation's files. Managed qualification uses
the package's bundled pure library sources; a development `stdlibSource`
override must be cleared or used with an external development backend.

Run **xMind: Upgrade Local Backend (Preserve Profile)** to apply the configured
runtime and edit policy to an owner that supports the native protocol. This is
an explicit owner operation rather than a model tool. The adapter authenticates
the existing workspace, requests native quiescence and retains the issued
receipt. Native rejects busy work and owns the admission barrier.

The adapter records the target, source receipt and pending connection in private
extension state. It compares digests of saved public session/history/run/skill,
provider and model records before activation. Replacement startup uses the same
SQLite database and owner token, omits provider YAML reimport and other startup
configuration overrides, and remains closed until Native activates it.
Session/model/run/workflow selections carry their accepted origin to the new
connection. The adapter never opens or copies SQLite.

`xmind_admin observe-owner-exit PID PROCESS_BIRTH TIMEOUT_MS` provides the native
Windows OS observation used after retirement. The creation-time value is the
decimal string issued by authenticated owner status. The command retains the
actual process handle for its bounded wait, detects a reused PID and never
signals a process. An access error or timeout does not authorize replacement.

Retirement is dispatched once. If its response is lost, actual process exit
plus Native's persisted source/target checks resolve safe bootstrap; the adapter
does not replay the mutation. A reload can reconnect to an already prepared or
accepted owner. Unknown activation outcomes, changed records, unavailable
authentication or altered package/workspace bindings fail with storage retained.
Further failed-start/reload and concurrent-command acceptance remains required.

**xMind: Cancel Pending Backend Upgrade** is implemented for an unconsumed
quiescence receipt. Native resume must accept its exact receipt and authority;
consumed retirement cannot be rolled back. This command still needs independent
installed acceptance.

The actual two-generation fixture initially exposed the xlang3 Unicode working-
directory bug. The isolated SDK fix is pushed as `7b8b32ae`; its allowed suite
passed 51 of 53 checks, with both failures reproduced using the unchanged
baseline, and all 11 default performance cases passed. Five CPython peer checks
were excluded. No CPython interpreter or bridge was used.

Windows ancestor pins also prevented renaming a staging directory. Publication
now creates a fresh final generation, writes its complete verified manifest last
and reuses only a fully verified generation. Partial publications remain for
diagnosis. Existing files are never overwritten or recursively removed.

The complete gate passed **97 native contracts in 196.96 seconds**, with 626
mapped inputs unchanged. All **197 extension and 39 browser tests** passed with
39 view inputs unchanged. The actual C++/embedded-xlang3 fixture exercised the
production thin launcher, actual native process-exit observer, same-database
retirement/bootstrap/activation, a deliberately lost reply and host reload.
Its VS Code storage/environment, deployment metadata and unused package members
are synthetic; saved history/provider lists are empty and no provider or file
effects occurred. This is not installed or rendered writing acceptance.
[Exact evidence and retained failures](evidence/native-owner-adapter-local.json).

The installed `TestProj` owner is a legacy read-only generation without this
protocol. It cannot receive a fabricated receipt. Its explicit operator
shutdown/migration path, independently accepted packaging, actual provider/
history/metrics/skill continuity and rendered create/edit approvals remain
required before writing is ready there.

The later [native post-exit preparation increment](native-legacy-owner.md)
passed 98 contracts and provides a distinct legacy operator ticket. It does
not yet add legacy shutdown/adoption to this extension command or upgrade the
installed preview.

The separate [native legacy preflight](native-legacy-preflight.md) now verifies
the actual listener/process, image, database command line and authenticated
workspace. It does not yet connect shutdown/adoption to this extension command.
