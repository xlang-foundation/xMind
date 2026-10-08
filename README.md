# xMind

xMind is being built as a general-purpose single-agent and graph platform with a shared coding runtime. The C++ backend owns execution, providers, tools, permissions, sessions and protocols. Embedded xlang3 runs compatible scripts/pure-Python libraries and performs SQLite database I/O. CLI, browser UI and VS Code clients use the same local backend. Team-server features, PostgreSQL, WebRTC and the standalone Electron IDE are excluded from xMind OSS and reserved for CantorAI’s closed-source Nexus product.

OpenCode 2 is the coding feature reference; LiteLLM is the provider coverage reference. Their implementations are not the engine. See the [architecture](doc/architecture.md), [SVG](doc/architecture.svg), [pinned parity baseline](doc/PARITY.md) and [provider requirements](doc/MODEL_SUPPORT.md).

## OSS deployment scope

Run xMind locally with the VS Code plugin, webpage UI and native CLI. SQLite is the OSS database; C++ owns repository and encryption contracts, and embedded xlang3 performs SQLite I/O. Team-server features, PostgreSQL, WebRTC and the standalone Electron IDE belong to the separate closed-source Nexus project. See the [current OSS specification](doc/architecture.md).

xMind's target connection profiles are **Local** for single-user execution and
**Nexus** for binding the local agent/workspace to a team server over the shared
versioned protocol. Nexus owns shared PostgreSQL state and distributed
coordination; xMind does not access its database directly. Generic profile/client
support belongs in OSS, while the team-server implementation stays private.
Nexus profile enrollment is not yet implemented. See the
[profile boundaries](doc/architecture.md#local-and-nexus-connection-profiles).

## Current native product scope

The latest local native gate passed **58 contracts**, with **88 extension and 17 browser checks**, plus the native/browser integration contract. [Local evidence](doc/evidence/native-gemini-local-provenance.json). This includes the Gemini request component only; Gemini transport/live inference remains unfinished. The older hosted/live checkpoints below retain their stated scope.

Native checkpoint `46262d6ef7949a9caf778ccb6cf74733ef28b5ac` passed **52 native and 73 extension tests**, plus **14 browser tests**, in its [hosted gate](https://github.com/xlang-foundation/xMind/actions/runs/37709335756). This includes embedded-xlang3 persistence, encrypted credentials, authenticated server/CLI, native model/tool execution, reviewed file/process effects, MCP components, registered graph execution, Chat Completions/Responses adapters saved-key wire enrollment/rebinding and durable native browser access sessions. Native effects are real; provider contracts use labelled synthetic peers. [Exact gate evidence](doc/evidence/native-model-protocol-diagnostics-hosted-provenance.json), [Responses scope](doc/native-responses-provider.md), [graph scope](doc/native-graph-checkpoints.md), [process scope](doc/native-process-tools.md). These checks do not prove full coding or protocol parity.

The VS Code adapter uses the right secondary sidebar, with Explorer left and the composer/model selector at the bottom. It renders Markdown/code/history and supplied metrics, compares exact edit/creation snapshots, reviews executable-bound commands, reconstructs retained stdout/stderr, and reconnects without resubmitting work. The HTML browser shares that renderer and backend through a separate access adapter. The `6263630` hosted browser and packaging gates passed; newer protocol diagnostic renderer checks recorded **73 extension and 14 browser tests locally**. Actual browser checks verified authenticated refresh, a registered file-reading graph, visible failures and live Responses tool/edit outcomes. [Failure scope](doc/sidebar-run-failures.md). The browser preview now runs the verified `46262d6` native binary and matching access adapter; other previews retain their older runtimes. [Durable browser sessions](doc/browser-session-persistence.md) passed actual refresh and adapter/backend restart checks without login or execution replay. Initial migration enrollment returned HTTP 502 before retries succeeded; its cause remains unresolved. Sidebar renaming and CLI conversation navigation passed the later `4203b84` hosted gate (52 native, 72 extension and 13 browser contracts); actual browser rename, refresh, backend reopen and read-only installed CLI navigation also passed. Actual editor rename acceptance remains pending. [Live acceptance](doc/evidence/live-browser-cli-navigation-rename.json).

A live OpenAI `chat-latest` browser response was observed earlier. The later `gpt-5.6-sol` HTTP 400 identified a reasoning parameter restriction on the Chat wire. Native Responses enrollment now resolves that tested case: actual minimal text, repository read-tool continuation, one approved edit of a dedicated validation file and a registered Git status command completed using the saved encrypted key. The browser displays real tool/operation outcomes, retained command output and each response's supplied metrics. [Live provider evidence and limits](doc/live-provider-compatibility.md). One official external filesystem MCP read/approval/Responses continuation and one official A2A SDK text task/retry/restart flow also passed live acceptance. [MCP scope](doc/evidence/live-responses-mcp-diagnostic-read.json), [A2A scope](doc/evidence/live-a2a-responses.json). The original MCP protocol failure remains undiagnosed. Full coding/CLI/editor parity, broader provider and MCP/A2A live coverage, dynamic delegation/replanning, shell/background/PTY and the remaining in-scope OSS capabilities remain incomplete. Team-server features, PostgreSQL, WebRTC and Electron are outside OSS scope. [Dynamic-agent scope](doc/dynamic-agent-execution.md), [milestones](doc/milestones.md), [MCP setup](doc/native-mcp.md).

## Native build and local use

Interactive native `chat [SESSION [MODEL]]` supports successive requests through the shared server. The verified installed CLI has passed live Responses read/approved-edit checks against the browser backend. Conversation navigation and request-based titles passed the `4203b84` hosted native gate; they are installed in the browser preview, with actual read-only console navigation verified. [Interactive CLI scope](doc/native-interactive-cli.md). The existing one-shot commands remain available.

Chat, approval handling and account discovery passed hosted validation; newer
conversation navigation passed the later hosted gate, with browser preview installation and actual read-only CLI navigation verified as documented in its scope. Use
`Tools/agentflow.ps1 -Action Chat -Port PORT` with the server token privately
configured in `XMIND_AUTH_TOKEN`. Add `-Session ID` to resume, and optionally
`-Model ID` for that existing session. `-BinaryDirectory DIR` selects an installed
native distribution instead of the default build output. The public Chat entry
point passed an empty-chat local check using the exact hosted bundle; no provider
request was made for that check.

Use Windows x64, a C++20 Visual Studio toolchain, Node.js for editor/tests, and a built sibling xlang3 runtime with its supported SDK/modules. From this checkout:

```powershell
.\Tools\agentflow.ps1 -Action Build
```

This builds `Native/CMakeLists.txt` in Release and runs CTest. The root CMake entry point also delegates to `Native/`; the old xlang build and `Core` have been removed. The launcher defers builds while observed xlang3 benchmarks are live; deferral is not a test pass. [Native development](doc/native-development.md) describes runtime path overrides and prerequisites.

Configure `XMIND_AUTH_TOKEN` privately in the server/client environment (32–256 printable non-space characters). In one console:

```powershell
.\Tools\agentflow.ps1 -Action Serve -Port 8765
```

In another console with the same private token:

```powershell
.\Tools\agentflow.ps1 -Action Client -Port 8765 -ClientArguments @('health')
.\Tools\agentflow.ps1 -Action Client -Port 8765 -ClientArguments @('create-session', 'My project')
.\Tools\agentflow.ps1 -Action Client -Port 8765 -ClientArguments @('sessions')
```

Without a model, the server supports session inspection and model-free registered tool graphs but cannot invoke an agent model. To enable the read-tool loop, provide the actual `-Model`, `-ModelEndpoint`, `-ModelTools supported`, and `-Workspace`. Configure `XMIND_API_KEY` privately on the backend or use a stored `-CredentialId`. Chat Completions is the default wire; explicit `-ModelWire responses` uses the native Responses adapter. Interactive Settings uses saved native provider profiles; see [provider setup](doc/provider-setup.md) for supported wires and verification limits. Broader provider coverage remains required. The local token represents a full-access `local-owner`, not team authentication; see [deployment](doc/server-deployment.md).

Follow the [VS Code guide](extensions/vscode/README.md). `Tools/start-ui.ps1` starts this machine's isolated development host and persistent native preview without a model or seeded conversations.

No CPython executable or native extension is used. `PythonLibSource` selects allowed standard-library source for xlang3. Pure-Python packages must be installed through xlang3's pip; discuss missing native APIs before changing runtime code or adopting a workaround.

## Project history

xMind preserves the upstream repository history and license. The old xlang-based implementation and the later Python prototype were removed from the working source tree because the product now uses the native C++/xlang3 architecture. Earlier code remains recoverable through Git history.
