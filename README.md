# xMind

xMind is being built as a general-purpose single-agent and graph platform with a shared coding runtime. The C++ backend owns execution, providers, tools, permissions, sessions and protocols. Embedded xlang3 runs compatible scripts/pure-Python libraries and performs SQLite database I/O. CLI and VS Code clients use the same backend; Electron and remote views follow that contract.

OpenCode 2 is the coding feature reference; LiteLLM is the provider coverage reference. Their implementations are not the engine. See the [architecture](doc/architecture.md), [SVG](doc/architecture.svg), [pinned parity baseline](Documents/PARITY.md) and [provider requirements](Documents/MODEL_SUPPORT.md).

## Current native product scope

The latest native Release checkpoint passed **31 native and 30 extension contracts** in [isolated CI](https://github.com/xlang-foundation/xMind/actions/runs/37621404384), source `e93f28c7e6d7c214f1c0d5e049ad9408128b4ad5`: embedded-xlang3 persistence, encrypted local credentials, authenticated server/admin/CLI, model streaming and scheduling, workspace reads, model-selected approved edits and new-file creation, stored usage/timings, restart recovery, uncertain-edit inspection and registered MCP tools against independent and official modern/legacy SDK peers. Real subprocess/file effects are verified; inference uses labeled synthetic peers. Live-provider/coding-task completion remains unverified. [Complete CTest evidence](doc/evidence/native-file-creation-ci-ctest.log), [source/runtime provenance](doc/evidence/native-file-creation-ci-provenance.json), [creation scope](doc/native-file-creation.md).

The VS Code adapter uses the right secondary sidebar, with Explorer left and the composer/model selector at the bottom. It renders Markdown/code/history and supplied metrics, compares exact edit/creation approval snapshots in a read-only diff, names external MCP tool proposals, and reconnects without resubmitting work. **30 extension contracts** pass locally and in CI; actual-host token persistence/diff behavior was verified separately. The preview uses the tested CI bundle and has no model configured. [Creation-review evidence](doc/evidence/vscode-native-creation-review.log), [actual UI](doc/evidence/vscode-native-creation-bundle-sidebar.png).

Model-invoked edits/creation, native model selection/catalogue and stored usage/timings passed hosted native contracts. Process/shell tools, attributed reconciliation, complete coding workflows, provider coverage, remaining MCP protocol and broader SDK interoperability, A2A, graphs, team authorization/PostgreSQL and Electron remain incomplete. [Milestones](doc/milestones.md) distinguish verified components from pending scope. [MCP setup](doc/native-mcp.md) describes trusted configuration and encrypted credential provisioning.

## Native build and local use

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

Without a model, the server supports session inspection but cannot execute an agent. To enable the verified read-tool loop, provide the actual `-Model`, `-ModelEndpoint` Chat Completions URL, `-ModelTools supported`, and `-Workspace`. Configure `XMIND_API_KEY` privately on the backend or use a stored `-CredentialId`. Other provider wire families remain required. The local token represents a full-access `local-owner`, not team authentication; see [deployment](doc/server-deployment.md).

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
