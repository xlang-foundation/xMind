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
[browser guide](browser-view.md). Managed VS Code mode uses the actual opened
folder automatically and starts a separate local Native owner for it. No
`.code-workspace` file or manual backend token is needed for a single folder.
A multi-root workspace retains its folder set but currently needs one explicitly
selected active Native root. Ready owners and accepted work survive view closure,
extension deactivation and folder changes; the sidebar shows the verified
effective root. The packaged extension passed an actual opened-folder acceptance
on TestProj after the 88-contract native gate; its normal persistent window
retains VS Code's folder-trust requirement. Node adapter tests alone do not
establish that acceptance. See [opened-folder evidence](vscode-opened-workspace.md).

Configure `agentflow.runtimeDirectory`, optional `agentflow.stdlibSource` and,
for an installed extension, optional `agentflow.providerConfigPath` in
machine/global User settings. In an Extension Development Host, xMind imports
the repository's `.config/providers.yaml` automatically when that regular file
exists. The extension passes only the absolute path to the authenticated native
owner; Native parses the YAML and encrypts keys in SQLite. The extension never
reads key values or searches the opened project for provider configuration. An explicit external connection uses
`agentflow.backendMode=external` and `agentflow.backendUrl`, with its token in
SecretStorage. A wrong root or unavailable workspace identity blocks submission.
`Tools/start-ui.ps1` and its external-origin bootstrap retain their separate
development-preview scope; they are not proof of managed folder binding.

The VSIX's `native-runtime` directory must contain a verified manifest,
`xmind.exe`, `xlang3_runtime.dll`, `xlang3.exe`, `modules/xlang_json.x3pkg.dll` and
`modules/xlang_sqlite3.x3pkg.dll`. Stage only an already accepted paired Native
and SDK build. The source manifest supplies their exact hashes and native/license
inventory, Windows x64 platform, bridge-disabled status and revision bindings.
Use a fresh local profile for this package; no legacy package or profile
migration is supported. Existing installations remain intact.
Add pure standard-library source and its license separately:

```powershell
node extensions/vscode/scripts/package-native-runtime.mjs --stage `
  --bundle D:\path\to\accepted-bundle `
  --bundle-manifest D:\path\to\accepted-source-manifest.json `
  --bundle-manifest-sha ACCEPTED_MANIFEST_SHA256 `
  --stdlib-source C:\Python\Python314\Lib `
  --stdlib-license C:\Python\Python314\LICENSE.txt `
  --out D:\path\to\xMind\extensions\vscode\native-runtime
```

The output must be fresh. This file-only packager verifies/copies accepted
binaries, pure `.py` sources and notices; it never runs Native or CPython.
Required source includes `os.py`, `json/__init__.py`, `encodings/__init__.py` and
`importlib/__init__.py`. Accepted bundle notices belong under `licenses/`, and
the explicit standard-library notice becomes `licenses/Python-STDLIB-LICENSE`.
`npm run package` verifies the staged runtime during prepublish after building
browser assets. It fails if the bundle is absent, incomplete or changed.

The latest published native acceptance and its limitations are recorded in
[validation status](VALIDATION_STATUS.md), [Responses continuation](native-responses-reasoning.md)
and the [milestone ledger](milestones.md). Test counts belong to exact source
revisions. A build or adapter gate does not establish installed-preview or live
provider acceptance.

The obsolete Python/FastAPI bootstrap, package lock, probe and prototype HTTP
MCP peer have been removed. Pure-Python reuse under xlang3 remains allowed;
install any future required packages using xlang3's pip and discuss exact
missing native APIs before changing the runtime or choosing a workaround.

Private Nexus implements the team server, PostgreSQL, WebRTC and Electron IDE.
Generic Local/Nexus client protocol support belongs in OSS; private
implementations do not count toward xMind completion.
