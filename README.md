# xMind

xMind is a native agent platform and coding assistant for local projects. The
CLI, webpage and VS Code extension share one C++ backend for agents, tools,
models, sessions, approvals and execution history. Embedded xlang3 runs
compatible Python scripts and pure-Python libraries and performs SQLite I/O.

A coding agent uses the same runtime as a general agent. Start with the normal
agent/tool loop; add skills, named agents or graphs when the task needs them.
Agents and graphs can be authored with YAML/Markdown and trusted custom xlang3
programs through the native authoring API. See the [architecture](doc/architecture.md),
[diagram](doc/architecture.svg) and [documentation index](doc/README.md).

## Current preview

The latest recorded qualified local installation is **0.1.80**, including the
VS Code authentication-persistence fix. This is a development preview, without
a Marketplace or general-release claim. That installation is a preserved
baseline; it does not qualify subsequent source changes as a new release.
[Installed qualification](doc/evidence/vscode-auth-persistence-0.1.80-20261025.json).

The current delivery goal is reliable browser, VS Code and CLI coding through
the native C++ backend, embedded xlang3 and SQLite. Development now uses
CantorServe for real project exercises and qualification of existing features.
Keep original project tests and failed-run evidence, and verify restoration
without replaying completed effects. A local contract pass alone does not
establish installed or model-backed acceptance.

Current source work includes a tested fix for cold-start run selection and an
investigation of Windows compiler-server lifetimes. A matching xlang3 runtime
has been rebuilt from main; native and packaged qualification remain in progress.
The registered native inventory currently contains 189 tests, not a claim that
they have all passed on this source.

[Working v1 goal and current acceptance](doc/WORKING_V1_GOAL.md),
[validation history](doc/VALIDATION_STATUS.md),
[current endpoint and build evidence](doc/evidence/local-blockers-20261102.json).

## Use in VS Code

1. Install the supplied verified Windows x64 VSIX with **Extensions: Install
   from VSIX**. VS Code 1.140 or newer is required.
2. Open and trust the local project folder, then run **xMind: Open Workspace**.
   xMind uses the opened folder; no `.code-workspace` file or separate server
   launch is required. For several opened roots, select the active root explicitly.
3. The agent sidebar is on the right. Choose the saved provider and a discovered
   model beside the composer. **Settings** can enroll a provider if none is saved.
   Existing configured keys remain on the backend and do not need re-entry.
4. Send a coding task. Inspect file-change proposals and approve the exact edits.
   Configured trusted command profiles can run without a per-command prompt.

Use **xMind: Open Browser View** for the webpage attached to the same workspace
and profile. The local adapter enrolls the browser privately; no server-token
copy is needed. Closing a view leaves backend execution alive.

Machine-level runtime, provider/process YAML and profile-root settings are
documented in the [extension guide](extensions/vscode/README.md). Paths are
not imported from the opened project. After changing trusted runtime/process
configuration, use **xMind: Apply Trusted Runtime Configuration** for a
profile-preserving handoff.

## Use the console

A verified native distribution contains `xmind.exe` and its paired runtime,
modules and pure standard-library source. Keep that directory intact. With
`xmind.exe` on PATH, start the managed console for a project:

```powershell
xmind --workspace D:\Projects\Example chat
```

The default profile root is `%USERPROFILE%\.xMind\p`. If the editor uses a
custom `agentflow.profileRoot`, supply that same path with `--profile-root` to
share its backend and sessions. Reconnecting reuses the profile's saved provider
settings; omit first-launch configuration options unless you intend to change
the trusted launch configuration. No key belongs on the command line.

```powershell
xmind --workspace D:\Projects\Example sessions
xmind --workspace D:\Projects\Example profile-info
xmind --workspace D:\Projects\Example workspace init
xmind --workspace D:\Projects\Example workspace add-folder D:\Projects\Shared
xmind --workspace D:\Projects\Example workspace info
```

`workspace init` registers the first folder before group inspection or folder
attachment. Adding a folder attaches it to that workspace; it does not copy or
delete files. Existing runs retain their admitted folder snapshot.
Explicit `--port` is the separate operator-selected backend mode and requires
its authentication configuration. [Console commands](doc/native-interactive-cli.md).

Use `--mcp-config FILE` to import trusted MCP server configuration into a
managed profile. Applying a changed document requires `--upgrade-backend`;
the backend validates saved revisions and retired server IDs before handoff.
VS Code exposes the machine setting `agentflow.mcpConfigPath` and
**xMind: Apply Trusted Runtime Configuration**. [MCP setup](doc/native-mcp.md).

## Browser and development

The webpage uses the same renderer and native command/event contracts. Its top
bar opens Workspace Settings for switching workspaces, creating one from an
existing folder and attaching additional folders. The left pane browses files;
the right sidebar contains the current conversation, activity, approvals and
model selection. [Browser launch and behavior](doc/browser-view.md).

For source builds use Windows x64, a C++20 Visual Studio toolchain, CMake,
Node.js and the supported xlang3 SDK. The guarded build entry point is:

```powershell
.\Tools\agentflow.ps1 -Action Build
```

[Development](doc/DEVELOPMENT.md) covers explicit SDK/library paths, isolated
builds and native runtime packaging. Python commands, tests and pip run with
xlang3; CPython executables and CPython native extensions are not used. Pure
Python libraries from PyPI are allowed. xMind-required native gaps are fixed
in an isolated xlang3 branch.

## Scope and history

xMind OSS runs locally with SQLite, CLI, webpage and VS Code. Generic Local and
Nexus connection profiles belong to the public architecture. Workers execute
locally through standard xlang3 IPC; cross-node execution belongs to later
Cantor Cluster work and is outside this delivery goal. Private
Nexus owns the team server, PostgreSQL, WebRTC and standalone Electron IDE.

OpenCode and LiteLLM are references for workflow and provider research.
xMind implements its own C++ runtime and protocols; compatibility and feature
parity with those projects are not acceptance criteria.

This repository preserves upstream xMind history and its Apache-2.0 license.
The old xlang implementation and Python prototype are recoverable through Git
history and are not part of the current product.
