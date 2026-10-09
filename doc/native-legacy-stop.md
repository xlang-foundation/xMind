# Legacy stop and profile-preserving upgrade

The managed VS Code upgrade command now handles an authenticated legacy owner
whose native owner endpoint returns 404. It resolves the actual loopback
listener through the native administration executable, verifies the source
package against the saved owner manifest, and obtains the
[native authenticated preflight](native-legacy-preflight.md). A product dialog
requires **Stop and migrate** before dispatch. The token stays in the
extension's own SecretStorage/environment; provider keys are not read by the
adapter or requested again.

The adapter retains a complete target generation outside the workspace and
extension installation, saves the original profile directory, target, source
PID/birth/hash, operator ticket ID and pending origin, then dispatches the native
stop command once. The relevant native interface is:

```text
xmind_admin --db FILE --modules DIR --stdlib DIR stop-and-prepare-legacy-owner TARGET_ROOT MANIFEST_SHA256 WORKSPACE PORT SOURCE_IMAGE SOURCE_SHA256 WORKSPACE_ID AUTHORITY_ID PID PROCESS_BIRTH TICKET_ID approved|read-only confirmed-stop
```

Native qualifies the actual target package/workspace/authentication and requires
its bundled module/library paths. It opens the observed source database through
embedded xlang3 and holds `BEGIN IMMEDIATE` while checking all durable execution
and maintenance owners are idle and snapshotting the original tables. It
revalidates the actual listener, image, command line and authenticated workspace
under that writer fence before opening a termination handle for the exact
PID/birth pair. This is an explicit Windows process termination because the
legacy generation cannot execute modern retirement; it is not a graceful
shutdown or an issued quiescence receipt.

Native observes actual exit, acquires the canonical database lease, verifies the
same database file identity and publishes the legacy ticket in its held
transaction. Failure before termination rolls back and preserves the live
source. Failure after termination retains storage and reports operator recovery;
it does not silently restart the source or claim restoration. Busy execution,
changed preconditions, invalid ticket IDs and absent confirmation reject stop.

Lost command output and host reload use a separate native inspection:

```text
xmind_admin --db FILE --modules DIR --stdlib DIR inspect-legacy-ticket TARGET_ROOT MANIFEST_SHA256 WORKSPACE PID PROCESS_BIRTH SOURCE_SHA256 TICKET_ID read-only-inspection approved|read-only
```

This verifies actual source exit and the exact persisted source, target,
database, authentication and saved-table bindings under the lease. It does not
stop a process or create a missing ticket. A live source or absent/changed ticket
requires operator recovery; the adapter never repeats the stop command.

Replacement startup uses the same SQLite profile and token, with
`--legacy-owner-ticket`. Native keeps admission closed, verifies all original
table contents and performs migration/prepared-owner publication atomically.
The adapter accepts only the exact legacy ticket with a null modern bootstrap
receipt, then requests native activation with the new receipt/workspace
authority. Native verifies saved tables again and consumes the ticket in the
activation transaction. Provider YAML is not reimported. Accepted session/model/
run/workflow selections retain their IDs and move to the new connection origin.
Legacy stop dispatch cannot be undone by a view's cancellation command.

The actual production server/admin fixture verifies explicit native termination
of its owned idle legacy process, same-database preparation, exact read-only
ticket recovery, closed replacement admission and activation. The production
thin adapter fixture additionally exercises one stop dispatch, deliberately
lost stdout after a real committed ticket, recovery and host reload. Its initial
launch deliberately omits qualification to create a legacy fixture; VS Code
storage, confirmation choice, package provenance and unused executable members
remain synthetic. Native SQL/process/HTTP operations are real. Saved sessions
are populated, history/provider lists are empty, and no provider or file effects
occurred. The separate native core fixture checks encrypted credentials,
history fidelity, queued-owner refusal, rollback and ticket consumption.

Synthetic view checks verify cancellation and a changed source package prevent
dispatch/profile replacement. Independently verified packaging, installation
into the existing TestProj profile, live provider/history/metrics/skill
continuity and rendered create/edit approvals remain pending. Further legacy
failure-after-stop and concurrent host/command acceptance remains required.

The complete local gate passed **98 native contracts in 196.55 seconds**, with
631 mapped inputs unchanged. All **198 extension and 39 browser tests** passed,
with 41 view inputs (39 tracked plus two vendor assets) and 11 browser assets
unchanged during the view gate. The first view launcher failed before running
the suites because generated browser assets were missing; rebuilding those
assets from current source and using a fresh evidence directory passed.
[Exact source/runtime maps and limits](evidence/native-legacy-stop-local.json),
[native output](evidence/native-legacy-stop-local-ctest.log).

The later [versioned package increment](native-legacy-package.md) is installed
in the normal TestProj profile and is awaiting its migration confirmation.
Installed file-writing acceptance is still pending.
