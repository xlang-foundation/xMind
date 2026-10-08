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

Current source passed the complete local **65 native contracts in 113.16 seconds**, with zero failures/skips and no post-build exclusions. The new native CLI contract took **4.23 seconds** and exercises OpenAI, Claude and Gemini profile discovery, private environment-key setup, selection, xlang3 SQLite reopen with encrypted credentials, signed Gemini text/history/usage, stale ownership without retry and failed-turn status preserved through settings until a later actual successful turn. Public identity reflection is rejected for all three provider families; safe native provider diagnostics reach the CLI. Provider replies and keys are synthetic. Fresh browser/native integration also passed the rebuilt server and source-matched assets. Unchanged thin-client source retains the earlier verified **94 extension and 17 browser tests**; those suites were not rerun for this CLI change. [Local CLI evidence](doc/evidence/native-provider-profile-cli-local-provenance.json). Hosted validation of the new 65-contract source remains pending. No live Gemini inference, actual Gemini IDE acceptance or preview upgrade is claimed. [CLI scope](doc/native-interactive-cli.md).

The enrollment checkpoint `2f5e0f05e9bfadcf86b5508863da0ec5a9e78cfe` passed its exact [hosted gate](https://github.com/xlang-foundation/xMind/actions/runs/37743203538): **64 native contracts in 158.09 seconds**, **94 extension and 17 browser tests**, native/browser integration and VSIX verification, with zero failures/skips. This verifies native Gemini catalogue pagination, encrypted enrollment, model-specific tool policy and actual enrolled file execution/cancellation/signed SQLite replay with synthetic provider peers. It excludes the newer generic CLI controls and all-provider reflection changes above. [Hosted enrollment evidence](doc/evidence/native-gemini-enrollment-hosted-provenance.json). GenerateContent eligibility alone does not establish tool capability or live model acceptance. [Provider setup](doc/provider-setup.md).

Gemini gateway/history checkpoint `75f45f0a036f1ffbab8c4b157df364f3697b52a0` passed its [hosted gate](https://github.com/xlang-foundation/xMind/actions/runs/37738970918): **61 native, 91 extension and 17 browser contracts**, plus native/browser integration and VSIX verification, with zero failures or skips. Its native gate took **121.84 seconds**. [Exact scope](doc/evidence/native-gemini-history-hosted-provenance.json). This validates native signed receipts, separate tool identities, supplied usage, callback ordering and escaped large-response handling with synthetic provider fixtures. It excludes the newer agent and enrollment contracts. Earlier cleanup and transport gates remain documented in [validation status](doc/VALIDATION_STATUS.md).

The agent milestone `c9591fe79cad9a4253ac8088933f0c8a2848ded1` passed **62 native contracts locally in 100.44 seconds**, then its exact hosted **62 native, 91 extension and 17 browser contracts**, native/browser integration and VSIX verification, with zero failures/skips and **128.80 seconds** for the native gate. It includes actual Gemini agent/file execution and signed history replay after xlang3 SQLite reopen using synthetic provider replies. [Local scope](doc/evidence/native-gemini-agent-local-provenance.json), [hosted scope](doc/evidence/native-gemini-agent-hosted-provenance.json).

Native checkpoint `46262d6ef7949a9caf778ccb6cf74733ef28b5ac` passed **52 native and 73 extension tests**, plus **14 browser tests**, in its [hosted gate](https://github.com/xlang-foundation/xMind/actions/runs/37709335756). This includes embedded-xlang3 persistence, encrypted credentials, authenticated server/CLI, native model/tool execution, reviewed file/process effects, MCP components, registered graph execution, Chat Completions/Responses adapters saved-key wire enrollment/rebinding and durable native browser access sessions. Native effects are real; provider contracts use labelled synthetic peers. [Exact gate evidence](doc/evidence/native-model-protocol-diagnostics-hosted-provenance.json), [Responses scope](doc/native-responses-provider.md), [graph scope](doc/native-graph-checkpoints.md), [process scope](doc/native-process-tools.md). These checks do not prove full coding or protocol parity.

The VS Code adapter uses the right secondary sidebar, with Explorer left and the composer/model selector at the bottom. It renders Markdown/code/history and supplied metrics, compares exact edit/creation snapshots, reviews executable-bound commands, reconstructs retained stdout/stderr, and reconnects without resubmitting work. The HTML browser shares that renderer and backend through a separate access adapter. Earlier browser checks verified authenticated refresh, a registered file-reading graph, visible failures and live Responses tool/edit outcomes. [Failure scope](doc/sidebar-run-failures.md). The installed browser preview retains the verified `19d69dd` native backend and `6f32d215` view; newer Gemini source is not installed there. [Durable browser sessions](doc/browser-session-persistence.md) passed actual refresh and adapter/backend restart checks without login or execution replay. Initial migration enrollment returned HTTP 502 before retries succeeded; its cause remains unresolved. Sidebar renaming and CLI conversation navigation passed the earlier `4203b84` hosted gate (52 native, 72 extension and 13 browser contracts); actual browser rename, refresh, backend reopen and read-only installed CLI navigation also passed at that scope. Actual editor rename acceptance remains pending. [Live acceptance](doc/evidence/live-browser-cli-navigation-rename.json).

A live OpenAI `chat-latest` browser response was observed earlier. The later `gpt-5.6-sol` HTTP 400 identified a reasoning parameter restriction on the Chat wire. Native Responses enrollment now resolves that tested case: actual minimal text, repository read-tool continuation, one approved edit of a dedicated validation file and a registered Git status command completed using the saved encrypted key. The browser displays real tool/operation outcomes, retained command output and each response's supplied metrics. [Live provider evidence and limits](doc/live-provider-compatibility.md). One official external filesystem MCP read/approval/Responses continuation and one official A2A SDK text task/retry/restart flow also passed live acceptance. [MCP scope](doc/evidence/live-responses-mcp-diagnostic-read.json), [A2A scope](doc/evidence/live-a2a-responses.json). The original MCP protocol failure remains undiagnosed. Full coding/CLI/editor parity, broader provider and MCP/A2A live coverage, dynamic delegation/replanning, shell/background/PTY and the remaining in-scope OSS capabilities remain incomplete. Team-server features, PostgreSQL, WebRTC and Electron are outside OSS scope. [Dynamic-agent scope](doc/dynamic-agent-execution.md), [milestones](doc/milestones.md), [MCP setup](doc/native-mcp.md).

## Native build and local use

Interactive native `chat [SESSION [MODEL]]` supports successive requests through the shared server. New source adds generic provider-profile commands and explicit interactive `/profile ID REVISION` selection; listing/discovery does not silently rebind chat admission. Those controls passed the local 65-contract gate above and are not installed in the preview. The verified installed CLI has passed live Responses read/approved-edit checks against the browser backend. Conversation navigation and request-based titles passed the `4203b84` hosted native gate; they are installed in the browser preview, with actual read-only console navigation verified. [Interactive CLI scope](doc/native-interactive-cli.md).

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
