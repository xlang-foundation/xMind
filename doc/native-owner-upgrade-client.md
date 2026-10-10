# Managed native owner upgrades

## Current native profile path (under verification)

The xMind binary now accepts `--upgrade-backend` on a managed local-profile
command. It stages the current verified runtime in the profile store while
keeping the current runtime authoritative, binds the upgrade to the
authenticated workspace and owner receipt, retires the existing owner, then
starts and activates the replacement with the same encrypted
profile record, SQLite database, owner token and listener port. Pending handoff
state is kept in the protected profile record so reconnect can resume an
interrupted retirement or activation. Provider and graph YAML are not replayed
on replacement startup. The native browser/VS Code view adapter requests this
upgrade automatically when its verified runtime differs from the saved
profile; if active work or an in-flight admission prevents quiescence, it keeps
serving through the current owner and retries on a later view launch. The
console exposes the same operation through `--upgrade-backend`; an explicit
busy upgrade fails without preventing ordinary reconnects or graph continuation.
The VS Code extension then imports its configured provider YAML through the
native provider-profile API after connection.

Once an owner receipt is persisted, recovery is bound to the staged target
generation. It validates and launches that private copy even if the app package
has changed since staging; a later launch can upgrade again to the newer
package. Until the owner receipt exists, ordinary profile commands continue to
use the source runtime even when a busy upgrade intent is retained.

`Native/tests/local_profile_contract.mjs` now constructs an older synthetic
runtime generation, creates a real managed profile under it, and upgrades the
profile while checking the port, session, graph result and history survive.
The independent VS Code extension contract suite passes **263/263**, including
provider YAML import and workspace readiness ([captured output](evidence/vscode-extension-contract-20261010.log)). JavaScript syntax and patch
whitespace checks pass. The previous native source revision built in Release
with the current xlang3 runtime in `build/native-milestone-clean`; the busy-
upgrade recovery edits are not yet rebuilt. CTest is deferred by the build gate
while a live xlang3 performance run is active. The updated profile-upgrade
contract and installed `TestProj` continuity remain unverified. The installed
profile has not been mutated; live acceptance must follow the native contract.

## Current installed-profile boundary

On 2026-10-10, the live `TestProj` managed profile was observed at runtime
`57da085`, process 34252, port 49164. Its authenticated provider catalogue
returned revision 0 with no profiles. That runtime implements the native
owner-retirement protocol, but not provider-YAML import. It will be eligible
for the new automatic view launch upgrade after the native build and isolated
contract pass. Until then, the installed profile remains unchanged.

The current runtime imported all four provider identities from the shared YAML
into a separate real xlang3 SQLite profile without making a provider or model
request. See the
[exact evidence](evidence/provider-import-live-local-20261010.json).

The new native path now implements profile state, database, owner token and
port continuity. Its C++ contract and installed VS Code/browser-view continuity
remain unverified. Because persisted browser-view credentials bind to the
still-running view-host process, preserving the database alone does not prove
those sessions survive a real runtime replacement.

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
