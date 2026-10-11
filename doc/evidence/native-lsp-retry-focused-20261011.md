# Focused native retry and profile-contract rerun — 2026-10-11

The Release build succeeds after the native language-server client was changed
to retry the standard transient LSP response codes `-32800` (`RequestCancelled`),
`-32801` (`ContentModified`) and `-32802` (`ServerCancelled`) up to four
attempts, with a 50 ms delay between attempts. The Rust Analyzer contract passed
three consecutive runs.

One intervening full CTest run passed 133/134. Its only failure was
`native_local_profile_contract`: the Node harness killed an upgrade subprocess
after its fixed 90-second per-command limit. This contract's complete successful
run takes about 162 seconds. The subprocess ceiling is now 180 seconds and the
CTest ceiling 300 seconds. Re-running the contract through CTest passed:

```text
1/1 Test #113: native_local_profile_contract ....   Passed  161.79 sec
100% tests passed, 0 tests failed out of 1
Total Test time (real) = 161.81 sec
```

The full CTest suite was not rerun after this test-timeout adjustment. The
previous 134/134 full-suite evidence remains at
[native-full-release-ctest-20261011.log](native-full-release-ctest-20261011.log);
the intervening 133/134 run is preserved at
[native-lsp-retry-ctest-20261011.log](native-lsp-retry-ctest-20261011.log).
The coding loop still lacks acceptance for a compiler/build error being surfaced
back to the agent as actionable diagnostics; clean diagnostics and navigation
do not prove that repair loop.
