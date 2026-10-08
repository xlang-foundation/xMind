# Native xMind development

The maintained checkout is `D:\CantorAI2026\xMind`. C++ owns the local
agent/coding backend; embedded xlang3 performs SQLite I/O and runs compatible
scripts and pure-Python source. Native CLI, browser and VS Code clients share
that backend. See the [architecture](architecture.md) and
[documentation index](README.md).

## Prerequisites and runtime selection

Use Windows x64, a C++20 Visual Studio toolchain, CMake 3.24 or newer and Node.js.
The complete TLS contract suite also requires the OpenSSL command-line tool.
Build xlang3 separately with its CPython bridge disabled. Supply its supported
SDK, shared runtime and JSON/SQLite modules. `PythonLibSource` supplies allowed
standard-library source; no CPython interpreter or native extension is used.

Use explicit runtime paths when selecting an isolated SDK checkout. The local
build helper imports the selected runtime and does not rebuild it:

```powershell
.\Tools\native-milestone.ps1 -Action Build `
  -RuntimeSource D:\path\to\prepared-xlang3 `
  -RuntimeDirectory D:\path\to\prepared-xlang3\build\Release `
  -PythonLibSource C:\Python\Python314\Lib
```

For an already prepared sibling runtime, the default entry point is:

```powershell
.\Tools\agentflow.ps1 -Action Build
```

The helper checks for live xlang3 benchmarks, verifies vendored schema sources,
installs the independent Node SDK fixtures without lifecycle scripts, builds
Release and runs CTest. Exit 3 means the build was deferred; it is not a test
pass. Use this guard for local native builds. `Tools/ci-native.ps1` is restricted
to isolated GitHub runners. [CI inputs and verification](native-ci.md).

Fresh builds use `xMind\build\native`. The former sibling `AgentFlow` contains
old generated build artifacts, not another source checkout. Do not reuse its
CMake cache, which contains obsolete absolute paths. Native namespace and
target names using `agentflow` remain internal implementation identifiers.

CMake selections are `AGENTFLOW_XLANG3_SOURCE`, `AGENTFLOW_XLANG3_RUNTIME_DIR`,
`AGENTFLOW_XLANG3_SHARED`, `AGENTFLOW_XLANG3_LIBRARY` and
`AGENTFLOW_PYTHON_LIB_SOURCE`. The current SQLite prerequisite patch remains
under `runtime-prerequisites/` and is applied by isolated CI; its original scope
is recorded in [M1](checkpoint-m1.md).

## Server and console

Privately configure the same `XMIND_AUTH_TOKEN` in both consoles before using
these commands. Supply `-BinaryDirectory` to select an installed distribution:

```powershell
.\Tools\agentflow.ps1 -Action Serve -Port 8765
.\Tools\agentflow.ps1 -Action Client -Port 8765 -ClientArguments @('health')
.\Tools\agentflow.ps1 -Action Client -Port 8765 -ClientArguments @('sessions')
.\Tools\agentflow.ps1 -Action Chat -Port 8765
```

A new backend without a model supports inspection and registered model-free
graphs. Configure a provider through the native account discovery/settings
contract before agent inference. Provider keys are encrypted by the backend.
See [provider setup](provider-setup.md), [server deployment](server-deployment.md)
and [interactive CLI](native-interactive-cli.md).

## Clients and verification

Use the [VS Code guide](../extensions/vscode/README.md) or the
[browser guide](browser-view.md). `Tools/start-ui.ps1` launches the local
development host; client views observe backend state and do not own execution.

The latest published native acceptance and its limitations are recorded in
[validation status](VALIDATION_STATUS.md), [Responses continuation](native-responses-reasoning.md)
and the [milestone ledger](milestones.md). Test counts belong to exact source
revisions. Uncommitted dynamic-plan work is not a validated release.

The obsolete Python/FastAPI bootstrap, package lock, probe and prototype HTTP
MCP peer have been removed. Pure-Python reuse under xlang3 remains allowed;
install any future required packages using xlang3's pip and discuss exact
missing native APIs before changing the runtime or choosing a workaround.

Private Nexus implements the team server, PostgreSQL, WebRTC and Electron IDE.
Generic Local/Nexus client protocol support belongs in OSS; private
implementations do not count toward xMind completion.
