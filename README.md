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

The current local VS Code package is **0.1.30**. Its native source is
`838e2eb370b989b2d568a94c221af2d4247bfe1c`, paired with xlang3 SDK
`58a8aab9b40a464e38a741129b4804ac36e27a53`. This is a development preview;
no Marketplace publication or general release is implied.

| Check | Verified scope |
| --- | --- |
| Native backend | All 150 local Release contracts pass; frozen source, dependencies, SDK and binaries verify unchanged. |
| Clients | Packaged 0.1.30 passes 422 extension/147 browser fixtures locally and on hosted CI; all 2,448 archive files and 2,445 installed non-metadata payloads verify. Actual VS Code activation remains separate. |
| Installed live run | Through the normal managed-profile browser launcher, saved-key Claude reads through the official MCP filesystem server, creates one reviewed file and verifies it with a successful xlang3 process. |
| Persistence | The current coding session restores 22 messages and both completed roots without execution replay. The prior MCP session retains eight messages and 321 events. |
| Current webpage | Isolated headless Edge completes two real coding turns and separately creates/switches workspaces, attaches a folder, approves a file creation and runs xlang3 in that attached folder. Refresh and file previews work after workspace changes. |
| Still open | Actual VS Code activation, visible desktop testing, hosted native acceptance, broader protocol recovery and process-bound agent execution. |

Prior live ModelZoo coding checkpoints separately demonstrate reviewed file
changes, an actual Vue production build and Python API tests under xlang3.
The installed 0.1.30 browser assets match both tested live pages. The webpage
refreshes its explorer after observed agent operations while
preserving open folders, search and the selected preview. A real Claude file
creation appeared without a Refresh click. The same browser implementation is
now included in the installed extension package.
Its native runtime is byte-identical to the accepted 0.1.27 runtime; the
existing browser/backend processes and their history remain running. The
extension was installed through the Code CLI without opening an editor window.
Actual VS Code pane activation remains unverified.
[Installed package evidence](doc/evidence/coding-package-0.1.30-20261018.json).
[Workspace and idle-owner recovery checks](doc/evidence/rendered-workspaces-and-recovery-20261018.json)
restore 13 messages after explicit backend recovery. This does not establish
unattended or in-flight recovery; the model's requested bare two-line reply
format was not satisfied, although its file contents were correct.
[Automatic explorer refresh evidence](doc/evidence/rendered-explorer-auto-refresh-20261018.json).
[Exact current evidence and remaining work](doc/VALIDATION_STATUS.md).

Newer native source also passes the complete 151-contract local suite. It moves
process calls behind a backend-owned capability and retains unsuccessful
uncertain wait/stop results. That source has not yet replaced the installed
runtime; hosted native acceptance and process-bound agent execution remain open.
[Native source evidence](doc/evidence/process-tool-owner-20261018.json).

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
Nexus connection profiles belong to the public architecture; authenticated
Nexus enrollment and distributed worker execution remain unfinished. Private
Nexus owns the team server, PostgreSQL, WebRTC and standalone Electron IDE.

OpenCode and LiteLLM are references for workflow and provider research.
xMind implements its own C++ runtime and protocols; compatibility and feature
parity with those projects are not acceptance criteria.

This repository preserves upstream xMind history and its Apache-2.0 license.
The old xlang implementation and Python prototype are recoverable through Git
history and are not part of the current product.
