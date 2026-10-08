# xMind

xMind is being built as a general-purpose single-agent and graph platform with a shared coding runtime. The C++ backend owns execution, providers, tools, permissions, sessions and protocols. Embedded xlang3 runs compatible scripts/pure-Python libraries and performs SQLite database I/O. CLI and VS Code clients use the same backend; Electron and remote views follow that contract.

OpenCode 2 is the coding feature reference; LiteLLM is the provider coverage reference. Their implementations are not the engine. See the [architecture](doc/architecture.md), [SVG](doc/architecture.svg), [pinned parity baseline](Documents/PARITY.md) and [provider requirements](Documents/MODEL_SUPPORT.md).

## Current native product scope

Native checkpoint `626363026e49d97599be102d2d30f6c10333ca87` passed **52 native and 70 extension tests**, plus **12 browser tests**, in its [hosted gate](https://github.com/xlang-foundation/xMind/actions/runs/37705662926). This includes embedded-xlang3 persistence, encrypted credentials, authenticated server/CLI, native model/tool execution, reviewed file/process effects, MCP components, registered graph execution, Chat Completions/Responses adapters saved-key wire enrollment/rebinding and durable native browser access sessions. Native effects are real; provider contracts use labelled synthetic peers. [Exact gate evidence](doc/evidence/native-durable-view-sessions-hosted-provenance.json), [Responses scope](doc/native-responses-provider.md), [graph scope](doc/native-graph-checkpoints.md), [process scope](doc/native-process-tools.md). These checks do not prove full coding or protocol parity.

The VS Code adapter uses the right secondary sidebar, with Explorer left and the composer/model selector at the bottom. It renders Markdown/code/history and supplied metrics, compares exact edit/creation snapshots, reviews executable-bound commands, reconstructs retained stdout/stderr, and reconnects without resubmitting work. The HTML browser shares that renderer and backend through a separate access adapter. The `6263630` hosted browser and packaging gates passed; newer source checks recorded **72 extension and 13 browser tests locally**. Actual browser checks verified authenticated refresh, a registered file-reading graph, visible failures and live Responses tool/edit outcomes. [Failure scope](doc/sidebar-run-failures.md). The browser preview runs the verified `6263630` native binary and matching access adapter; other previews retain their older runtimes. [Durable browser sessions](doc/browser-session-persistence.md) passed actual refresh and adapter/backend restart checks without login or execution replay. Initial migration enrollment returned HTTP 502 before retries succeeded; its cause remains unresolved. Sidebar renaming and CLI conversation navigation passed the later `4203b84` hosted gate (52 native, 72 extension and 13 browser contracts); actual preview/editor installation remains pending. [Exact later gate evidence](doc/evidence/native-session-navigation-hosted-provenance.json).

A live OpenAI `chat-latest` browser response was observed earlier. The later `gpt-5.6-sol` HTTP 400 identified a reasoning parameter restriction on the Chat wire. Native Responses enrollment now resolves that tested case: actual minimal text, repository read-tool continuation, one approved edit of a dedicated validation file and a registered Git status command completed using the saved encrypted key. The browser displays real tool/operation outcomes, retained command output and each response's supplied metrics. [Live provider evidence and limits](doc/live-provider-compatibility.md). Full coding/CLI/editor parity, broader provider and MCP/A2A live coverage, dynamic delegation/replanning, shell/background/PTY, team authorization/PostgreSQL, WebRTC and Electron remain incomplete. [Dynamic-agent scope](doc/dynamic-agent-execution.md), [milestones](doc/milestones.md), [MCP setup](doc/native-mcp.md).

## Native build and local use

Interactive native `chat [SESSION [MODEL]]` supports successive requests through the shared server. The verified installed CLI has passed live Responses read/approved-edit checks against the browser backend. Conversation navigation and request-based titles passed the `4203b84` hosted native gate; they are not yet installed in the live preview. [Interactive CLI scope](doc/native-interactive-cli.md). The existing one-shot commands remain available.

Chat, approval handling and account discovery passed hosted validation; newer
conversation navigation passed the later hosted gate, while live installation remains pending as documented in its scope. Use
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

This builds `Native/CMakeLists.txt` in Release and runs CTest. The root CMake target and `Core` are legacy migration references. The launcher defers builds while observed xlang3 benchmarks are live; deferral is not a test pass. [Native development](doc/native-development.md) describes runtime path overrides and prerequisites.

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

Without a model, the server supports session inspection and model-free registered tool graphs but cannot invoke an agent model. To enable the read-tool loop, provide the actual `-Model`, `-ModelEndpoint`, `-ModelTools supported`, and `-Workspace`. Configure `XMIND_API_KEY` privately on the backend or use a stored `-CredentialId`. Chat Completions is the default wire; explicit `-ModelWire responses` uses the native Responses adapter. Interactive Settings currently enrolls Chat Completions only. Other provider families remain required. The local token represents a full-access `local-owner`, not team authentication; see [deployment](doc/server-deployment.md).

Follow the [VS Code guide](extensions/vscode/README.md). `Tools/start-ui.ps1` starts this machine's isolated development host and persistent native preview without a model or seeded conversations.

No CPython executable or native extension is used. `PythonLibSource` selects allowed standard-library source for xlang3. Pure-Python packages must be installed through xlang3's pip; discuss missing native APIs before changing runtime code or adopting a workaround.

## Historical upstream guide

The original text below describes the old xlang implementation. Preserve it as migration reference; use the native instructions above for current development. Historical Python/FastAPI results likewise do not establish native product acceptance. See [migration history](Documents/MIGRATION.md).

### Original xMind -- AgentFlow Framework

**xMind** is a modular framework built with XLang, designed to implement Large Language Model (LLM) Memory, Planning, and Agent-flow capabilities. This project allows developers to seamlessly integrate advanced AI features like context retention, decision-making, and dynamic dataflows into their applications.

## Features

- **LLM Memory**: Retain and utilize context across sessions to enhance interaction and decision-making.
- **Planning**: Implement sophisticated planning mechanisms that allow LLMs to make informed decisions based on historical data and projected outcomes.
- **Agent-flow Management**: Orchestrate complex Agent-flows to streamline processing and enhance the performance of AI-driven applications.
- **Modular Design**: Easily extend and customize the framework to fit your specific needs.  

[AgentFlow Graph](./AgentFlow.md)

## Getting Started

### Prerequisites

- [XLang](https://github.com/xlang-foundation/xlang) Please clone XLang into the xMind/ThirdParty folder and ensure the folder is named xlang.

### Build the Framework

Clone the repository:

```bash
git clone https://github.com/xlang-foundation/xMind.git
cd xMind
mkdir build
cd build
cmake ..
make

```
for xcode, use cmake -G Xcode .. to generate Xcode project
## Terms and Concepts

1. **Blueprint**: A YAML-based structure used to define various elements such as variables, prompts, actions, and more.

2. **Variable**: 
   - **Scope**: Variables are global within the same file and do not require a prefix. 
   - **Cross-File Access**: When accessing a variable from another file, a prefix must be used, e.g., `file1.var1`.

3. **Node**: 
   - Represents a component in AgentFlow, using a graph-based approach to connect various nodes.

4. **Function**:
   - A node within AgentFlow that serves as an inline translate node. It supports only one input and one output.

5. **Action**:
   - A buffered node in AgentFlow that processes input through a separate thread (in XLang) or a process (in Python).
   - **Use Case**: Actions are typically used to connect to external environments such as REST APIs, file access, or UIs.

6. **Agent**:
   - A specialized node within AgentFlow that performs LLM (Large Language Model) inference. 
   - **Core Node**: It serves as the core of AgentFlow, buffering inputs and combining them with prompts from various sources before making an inference request to an LLM.

7. **LlmPool**:
   - Managed by xMind, this concept involves handling LLM requests in a pool, based on factors like HTTP request status and LLM key usage time limits.
8. **Session Memory**: 
   - Session Persistence: Each chat completion is maintained within a session, ensuring continuity across interactions.
   - Session Identifiers: Externally, each session is identified by a globally unique identifier (GUID). Internally, sessions are tracked using an integer that loops for efficient resource management.
   - Node Data Handling: The first item in each node’s input and output data is the internal session ID. This approach allows a single graph instance to serve multiple chat instances, optimizing resource usage.
   - Session Memory: Sessions maintain a history of interactions as session memory. When making requests, this history is automatically bound as part of the prompt, ensuring context is preserved.
   - LLM Output Integration: All outputs from the language model (LLM) are fed back into the session memory, continuously enriching the session’s context.
### Running the Framework
  [Start Guide](./Start.md)

### CLI - xmcli
  [CLI](./xmcli.md)
