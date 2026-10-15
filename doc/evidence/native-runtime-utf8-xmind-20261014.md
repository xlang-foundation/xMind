# Windows xlang3 UTF-8 runtime integration checkpoint

This checkpoint verifies xMind's native backend against the xlang3 runtime
branch that fixes Windows UTF-8 import-root and runtime path handling.

| Component | Revision / result |
| --- | --- |
| xMind source base | `ea926d1` (`main`; local dynamic-plan changes are included in the tested working tree) |
| xlang3 source | `7b8b32ae3a0e6a99fac7babd97362736099448fb` (`xmind/windows-import-root-utf8`) |
| Native Release build | Passed |
| Native CTest suite | 142/142 passed, parallelism 8 |
| VS Code extension tests | 352/352 passed |
| Browser tests | 112/112 passed |

The probe build used `build/native-dynamic-plan-utf8probe` so it did not replace
the existing build configured against `nexus/mmap-buffer-export`. The three
previously failing contracts passed with the UTF-8 runtime: managed local
profile startup with a Unicode workspace, browser/local-view operation, and
handoff into a package generation whose path contains Unicode. The two
corrected graph HTTP fixtures also passed in the full suite. This gives direct
evidence that the older runtime's path conversion caused the startup/import
failures.

Reproduce the build by configuring `Native` with
`AGENTFLOW_XLANG3_SOURCE=D:/CantorAI2026/.worktrees/xlang3-xmind-import-utf8`,
`AGENTFLOW_XLANG3_RUNTIME_DIR` set to that checkout's `build/Release`, and the
matching `AGENTFLOW_XLANG3_LIBRARY` and `AGENTFLOW_XLANG3_SHARED` files, then
building Release and running:

```powershell
ctest --test-dir build/native-dynamic-plan-utf8probe -C Release --output-on-failure --parallel 8
```

The tests use synthetic provider replies and make no live provider requests.
They are integration evidence for this source/runtime pairing, not release
acceptance, an installed-editor acceptance, or provider acceptance. A packaged
xMind release still needs an accepted xlang3 revision containing the UTF-8
changes and a manifest pairing it with the exact Native binaries.
