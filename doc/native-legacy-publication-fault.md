# Legacy publication failure after stop

Native legacy stop now preserves its process-outcome diagnostic even if SQLite
rollback also fails. Once termination has occurred, an exception reports that
the source stopped and preparation remains unverified. It never reports that
the old owner resumed. Before termination, rollback failure explicitly reports
that the source was not stopped and needs operator recovery.

The thin adapter now gives a specific message when the source exited but the
exact saved ticket cannot be inspected. It retains the profile and pending
operation and does not repeat stop, restart the source or create a replacement
database. A missing ticket is not interpreted as permission to retry a mutation.

The actual production server/admin fixture adds a deferred foreign-key fault
on legacy-ticket publication. The test observes real native source termination,
the CLI's failure reply, absent ticket and empty fault row after rollback, and
the retained saved session. Its C++ helper accesses only owned temporary
`xmind-handoff-*` databases through embedded xlang3. The helper then removes its
fault trigger as an explicit fixture operator repair. A separate native
post-exit preparation produces a fresh ticket; qualified bootstrap and exact
activation preserve the same saved session. That real preparation reply is kept
separate from the failed stop reply. No successful stop response is fabricated.

This validates the post-stop publication rollback path using an actual process
and native/xlang3 SQLite. It does not inject a rollback-call failure or validate
every storage/crash failure. Package metadata remains synthetic, no providers
or file effects occur, and this is not installed migration acceptance.

The installed version 0.1.1 remains at its user confirmation. These source
changes have not replaced its retained target or been installed while that
operation is pending. [Installed package and current scope](native-legacy-package.md).

The complete local gate passed **98 native contracts in 205.33 seconds**, with
631 mapped inputs unchanged and every expected/registered/passed contract
matched. All **198 extension and 39 browser tests** passed with 41 view inputs
and 11 browser assets unchanged. No CPython interpreter or bridge was used;
the paired SDK remains `7b8b32ae`.
[Exact tested source, runtime and fault scope](evidence/native-legacy-fault-local.json),
[native output](evidence/native-legacy-fault-local-ctest.log).
