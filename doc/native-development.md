# Native core development

Architecture: [architecture.md](architecture.md) and [architecture.svg](architecture.svg). The user approved continued work on the C++ core with embedded xlang3 for scripts and skills. OpenCode and LiteLLM remain references only.

Latest verified scope: the [isolated Windows CI run](https://github.com/xlang-foundation/xMind/actions/runs/37595553681) passed Release compilation and **nineteen native contracts**, including model-selected approved edits, stored usage/timings and read-only uncertain-file inspection. [Complete log](evidence/native-model-edit-ci-ctest.log), [pinned runtime/toolchain provenance](evidence/native-model-edit-ci-provenance.json). The older eighteen-contract/source-pending statements below preserve earlier checkpoints. Live providers, complete coding tools, protocols/graphs and team/Electron scope remain incomplete.

Current native evidence: the last verified Release run passed [eighteen contracts](evidence/native-approval-api-ctest.log). The HTTP/CLI, encrypted credentials, scheduling/model/read-tool loop and approved edit components are verified at the documented test scopes. Model-invoked edits and new model/response metadata changes await native verification; live providers, full coding workflows, protocols/graphs and team deployment remain incomplete. [Milestones](milestones.md) are the authoritative checkpoint ledger. Earlier component-status paragraphs below are historical.

Use `Tools/agentflow.ps1 -Action Build` from the repository root for the guarded build/test workflow. It does not modify the sibling xlang3 runtime and defers while observed measured benchmark processes are live.

The independent `Native/CMakeLists.txt` builds `agentflow_core`, not the legacy `ThirdParty/xlang` application. The default target contains a backend ownership lease, `XlangSqlite` and `Repository`: C++ session/run/event/message and general JSON information contracts, with all SQLite I/O through embedded xlang3. HTTP service, execution engine, encrypted credential storage, protocols, graph checkpoints and CLI remain to be implemented.

## Dependencies

The default target imports the existing xlang3 shared Release runtime and supported SDK. Database I/O uses xlang3's `sqlite3` interface. Runtime/package and allowed standard-library source paths are explicit; no CPython process or CPython native extension is used. CMake does not rebuild or modify xlang3. No OpenCode code or LiteLLM SDK is linked.

The earlier direct-SQLite store is a disabled migration reference, enabled only with `AGENTFLOW_BUILD_DIRECT_SQLITE_REFERENCE=ON`. Its amalgamation pins and contracts are historical material; passing them would not validate the embedded-xlang3 repository. Production must not select it as a workaround for runtime failures.

## Build and check

On an idle machine, using the Visual Studio CMake executable if `cmake` is not on PATH:

```powershell
cmake -S Native -B build/native -G 'Visual Studio 18 2026' -A x64
cmake --build build/native --config Release
ctest --test-dir build/native -C Release --output-on-failure
```

Use `AGENTFLOW_XLANG3_SOURCE`, `AGENTFLOW_XLANG3_RUNTIME_DIR`, `AGENTFLOW_XLANG3_SHARED` and (Windows) `AGENTFLOW_XLANG3_LIBRARY` to select the SDK and existing Release runtime. `AGENTFLOW_PYTHON_LIB_SOURCE` selects standard-library source files only. C++20 is required. Build artifacts stay in AgentFlow's build directory; the test stages its runtime DLL there on Windows. These commands are prepared instructions, not evidence that this target has compiled or passed.

## Contracts under test

`Native/tests/repository_contract.cpp` now tests the target xlang3-backed repository: cross-connection information/state, JSON validation and rollback, active-root conflicts, state/event atomicity, stale transitions, terminal-event guards, reopen/replay, lease-guarded recovery and injected creation failure. It is uncompiled/unverified. Actual concurrent owner-thread workloads and broader graph/approval contracts still need tests. Every mutation's affected-row checks remain intact; an unexpected native `-1` fails and rolls back instead of silently succeeding.

The default `Native/tests/xlang_sqlite_contract.cpp` covers parameterized C++-to-xlang3 SQL calls, binary blobs, embedded-null text, integer/real/NULL values, affected rows, insert IDs, transaction rollback and rejection of cross-thread runtime access. The adapter preserves native rowcount values, including `-1`, rather than hiding classification errors.

The following store/recovery contracts describe the disabled direct-SQLite reference. They must be ported to the target repository before its behavior is established.

`Native/tests/store_contract.cpp` covers shared state across connections, embedded-null text persistence, atomic state/event rollback on invalid JSON and injected insertion failure, stale and invalid state transitions, reserved lifecycle events, rejection of late execution events, durable messages and event cursors, competing run creation and terminal transitions, explicit/idempotent restart recovery, preservation of paused runs, and refusal to adopt a prototype or another application's database. Lease cases cover same-process/path aliases, an independent child process and reacquisition after release. Test checks remain active in Release builds.

Recovery now requires a `BackendLease` for the same database. Lease source uses a persistent sidecar and an exclusive Windows handle ([CreateFileW](https://learn.microsoft.com/en-us/windows/win32/api/fileapi/nf-fileapi-createfilew)); the POSIX path uses nonblocking [flock](https://man7.org/linux/man-pages/man2/flock.2.html). Ownership depends on the held OS resource, not sidecar existence or PID text. Database paths resolve aliases; databases with hard-link aliases are rejected. The sidecar is not deleted when releasing ownership. The store never recovers automatically on open. Service integration, forced-crash recovery testing, graph/effect reconciliation, approvals, full session lifecycle, artifacts, snapshots and schema upgrades still require native implementations and broader checks.

## Current validation

Current native result: Release compilation passed; all three default contracts passed ten consecutive runs after the authorized xlang3 SQLite module fixes. Independent demo seed/read and cursor replay passed. See `milestones.md` and `build/native/evidence/contracts-repeat-after-sqlite-fix-20261006.log`. The earlier failures described below are retained as history. The historical direct store is still disabled. Encryption is a tested standalone component; encrypted database credentials and the HTTP/agent/protocol/UI layers are not yet delivered.

The independent Windows secret-protection target compiled in Release and its contract passed. Default native tests now include that passing component plus the two blocked SQLite tests; complete evidence is preserved in `build/native/evidence/contracts-20261006.log`. See `credential-storage.md` for verified scope and pending storage/service integration. No xlang3 native source was changed by this component.

The default core, adapter/repository tests and persistence demo compiled in Release. Both default tests failed before SQLite connection creation completed: `isolation_level` is an invalid keyword argument for `Connection()`. The milestone demo has not passed. Runtime source is unchanged pending discussion of the scoped SQLite fixes. Preserve `build/native/Testing/Temporary/LastTest.log`; do not remove the keyword or weaken affected-row assertions to claim compatibility. A successful historical Python prototype test is not native-core evidence. The earlier reference store remains disabled and unverified.

The already-built sibling `build/Release/xlang3_native_package_real_tests.exe` was executed against `build/Release/modules` with the allowed standard-library source directory; it exited 0. This verifies the stock SDK's native JSON/SQLite package import and basic SQLite query path. It does not validate the new adapter, binary/transaction contracts, affected-row reliability or the target repository.
