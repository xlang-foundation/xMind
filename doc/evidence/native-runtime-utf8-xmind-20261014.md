# Windows xlang3 UTF-8 runtime integration checkpoint

This checkpoint verifies xMind's native backend against the current xlang3
`main` runtime, including Windows UTF-8 import-root and runtime path handling.

| Component | Revision / result |
| --- | --- |
| xMind source | `79a0fc63d603afd5f392f4756d504bf9dd580449` (`main`) |
| xlang3 source | `f6bbc561673e3c166dc0dab9062cb24db26432a1` (`main`) |
| Native Release build | Passed |
| Native CTest suite | 142/142 passed, parallelism 8, against xlang3 `main` |
| VS Code extension tests | 352/352 passed |
| Browser tests | 112/112 passed |

The primary build directory `build/native-dynamic-plan` was reconfigured from
the stale `nexus/mmap-buffer-export` runtime to xlang3 `main`, then rebuilt.
The three previously failing contracts pass on xlang3 `main`: managed local
profile startup with a Unicode workspace, browser/local-view operation, and
handoff into a package generation whose path contains Unicode. The corrected
graph HTTP fixtures also pass in the full suite. This isolates the earlier
startup/import failures to the stale runtime selected by the old build cache;
the xMind-focused older branch is not required for this integration.

The primary build was configured with
`AGENTFLOW_XLANG3_SOURCE=D:/CantorAI2026/xlang3`,
`AGENTFLOW_XLANG3_RUNTIME_DIR=D:/CantorAI2026/xlang3/build-pytest-pathaware/Release`,
and the matching `AGENTFLOW_XLANG3_LIBRARY` and `AGENTFLOW_XLANG3_SHARED` files.
Reproduce the test gate with:

```powershell
ctest --test-dir build/native-dynamic-plan -C Release --output-on-failure --parallel 8
```

The tests use synthetic provider replies and make no live provider requests.
They are integration evidence for this source/runtime pairing, not release
acceptance, an installed-editor acceptance, or provider acceptance. A packaged
xMind release still requires the normal accepted runtime manifest pairing the
exact xlang3 and Native binaries.
