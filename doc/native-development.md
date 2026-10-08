# Native xMind development

The target architecture is the native C++ core with embedded xlang3 for scripts, pure-Python libraries and database I/O. OpenCode and LiteLLM are behavior/coverage references. See [architecture](architecture.md) and [milestones](milestones.md).

The checkout on this machine is `D:\CantorAI2026\xMind`; its sibling runtime is `D:\CantorAI2026\xlang3`. Source, Git history, the editor profile and the session database have moved into xMind. Only earlier locked build artifacts remain under the former `AgentFlow\build` directory. Fresh builds use `xMind\build\native`; never reuse the old CMake cache with its absolute source paths.

Release compilation and all **35 native and 40 extension contracts** passed for source `3297a4bce2d591c0f1c1dd37ccc18a15297369e7` in [isolated CI](https://github.com/xlang-foundation/xMind/actions/runs/37635775031). Actual server/admin/CLI, subprocess/file and embedded-xlang3 database effects verify approved edits/creation/MCP tools, executable-bound foreground commands, descendant cleanup, bounded durable output, watcher reconnect and storage-fault quarantine/recovery. Inference is synthetic. [CTest evidence](evidence/native-process-passing-ci-ctest.log), [full hosted job/TAP log](evidence/native-process-passing-ci-job.log), [provenance](evidence/native-process-passing-ci-provenance.json). The actual unseeded sidebar now uses this tested bundle with its existing database/profile preserved. Local heavy compilation remains deferred during the independent timing controller. Full SDK/shell/background/PTY/provider/graph/team coverage and live coding remain required.

## Build and run

Use Windows x64, a C++20 Visual Studio toolchain and Node.js for independent protocol fixtures and the VS Code host adapter. The default target imports the existing xlang3 shared runtime and supported SDK. Database operations use xlang3's SQLite interface. The reviewed native SQLite prerequisite and allowed standard-library source are documented in [isolated CI](native-ci.md). No CPython interpreter, bridge or native extension binary is used. CMake does not rebuild or modify xlang3.

From the checkout:

```powershell
.\Tools\agentflow.ps1 -Action Build
```

The guarded launcher checks for live xlang3 benchmark processes, verifies all pinned jsoncons source/license hashes, configures Release, builds native targets and runs CTest. Exit 3 means deferred, not passed. Runtime/stdlib overrides are available through `RuntimeDirectory` and `PythonLibSource`; the latter supplies compatible source files only.

For direct toolchain use on an idle machine:

```powershell
npm.cmd ci --prefix Native/tests/sdk --ignore-scripts --no-audit --no-fund
cmake -S Native -B build/native -G 'Visual Studio 18 2026' -A x64
cmake --build build/native --config Release
ctest --test-dir build/native -C Release --output-on-failure
```

`AGENTFLOW_XLANG3_SOURCE`, `AGENTFLOW_XLANG3_RUNTIME_DIR`, `AGENTFLOW_XLANG3_SHARED`, `AGENTFLOW_XLANG3_LIBRARY` and `AGENTFLOW_PYTHON_LIB_SOURCE` select explicit SDK/runtime/source paths. `BUILD_TESTING` requires the independent Node fixtures; the isolated gate additionally verifies the complete named contract set. The superseded direct-SQLite implementation and historical root `Core` target were removed. Database operations use embedded xlang3.

See [server/CLI](native-server.md), [trusted MCP configuration and credentials](native-mcp.md), and [editor setup](../extensions/vscode/README.md). `Tools/start-ui.ps1` opens this machine's normal development host with its persistent profile and database. Its current isolated tested bundle has no model or seeded conversations; the actual [preview](evidence/vscode-native-creation-bundle-sidebar.png) has Explorer left and the xMind sidebar/composer/model chooser right.

## Remaining product scope

Live-provider/coding-task validation, process/shell tools, attributed reconciliation, provider wire-family coverage, broader MCP SDK interoperability/HTTP/OAuth/resources/prompts, A2A, graph execution and team authorization/PostgreSQL remain required. Electron and WebRTC adapters follow the shared backend contract and remain incomplete. Schema-worker budgets are actual resource/lifetime limits; they do not constitute a filesystem/network sandbox or full upstream schema conformance. The [milestone ledger](milestones.md) retains earlier component evidence and exact validation scopes.
