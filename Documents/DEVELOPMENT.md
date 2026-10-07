# AgentFlow development

The following commands run the historical Python/FastAPI prototype. The latest user requirement is native C++ platform implementation; these commands and tests do not establish native completion. Native build/run instructions will replace this workflow once the new target is implemented. See `NATIVE_ARCHITECTURE.md`.

## Setup

Build the sibling xlang3 checkout using CMake and Visual Studio, Release/x64. Install application dependencies into a local environment:

```powershell
.\Tools\setup.ps1
```

All Python application and test execution uses xlang3. Setup bootstraps pip from its pure-Python bundled wheel without starting CPython, then executes pip with xlang3. `requirements-pure.lock` pins the application packages, installed from PyPI as platform-independent wheels into `.agentflow/site-packages`. Transitive dependencies are explicitly pinned because automatic native dependency installation is disabled.

Pydantic-core distributes Python wrappers and native binaries in one wheel. Setup downloads that wheel through xlang3 pip and extracts only `.py`, `.pyi`, and `py.typed` wrapper files. The CPython binary stays in an unexposed download cache and is never installed or loaded; xlang3 supplies the native binding. Setup scans the application directory for native binaries and runs the FastAPI dependency probe. This clean setup passed.

Adjust `-Runtime` and `-PythonLib` on the launcher for another machine. `-PythonLib` supplies standard-library source files to xlang3; it does not start a CPython interpreter. Missing native capabilities must be discussed with the user before native changes or workarounds. The old `.venv` and `requirements.lock` remain historical dependency audit artifacts and are not used by the application launcher.

## Backend and CLI

Configure `AGENTFLOW_MODEL`, `AGENTFLOW_API_KEY`, and optionally `AGENTFLOW_BASE_URL` (an OpenAI-compatible `/v1` base URL). Keys stay in the backend environment; never pass them as CLI arguments or commit them.

```powershell
.\Tools\agentflow.ps1 serve --workspace D:\YourProject
.\Tools\agentflow.ps1 session --title 'Fix a bug'
.\Tools\agentflow.ps1 sessions
.\Tools\agentflow.ps1 run --session SESSION_ID --prompt 'Read the README and explain this project'
.\Tools\agentflow.ps1 events --run RUN_ID
.\Tools\agentflow.ps1 cancel --run RUN_ID
```

The backend defaults to read-only workspace tools. Enable mutation explicitly with `serve --allow-write`, or select per-operation decisions with `serve --ask-write`. Writes return before/after content in durable tool events. Approval API/CLI/editor controls are implemented but their end-to-end check is blocked by the SQLite failure; see `PERMISSIONS.md`. Snapshot restoration is still pending. The backend binds to loopback for local development; authentication and multi-process ownership must be completed before production operation.

Events accept `--after EVENT_SEQUENCE` to reconnect without replaying older events. Sessions and model/tool conversation history persist in `.agentflow/state.sqlite`. Interrupted runs are marked failed at startup; checkpoints and automatic continuation remain pending.

## Validation

Set `XLANG3_PYTHON_LIB` to the Python 3.14 `Lib` source directory and `PYTHONPATH` to the project root plus `.agentflow/site-packages`, then run the following through xlang3:

```text
tests/runtime_probe.py
tests/store_probe.py
tests/server_probe.py
tests/engine_probe.py
tests/mcp_server_probe.py
tests/mcp_client_probe.py
```

The engine probe uses an HTTP test double to exercise streaming, fragmented tool arguments, a file-read/model continuation, usage events, write denial, workspace boundaries, cancellation and timeouts. It does not establish live provider compatibility. Real providers, MCP client/stdio/authentication, A2A, graphs, compaction, richer coding tools, full VS Code validation, and complete OpenCode parity remain under development.

## VS Code extension

The development extension lives in `extensions/vscode`. Run `npm ci`, `npm test`, and `npm run package` there to produce `agentflow-0.1.0.vsix`. Client tests cover actual HTTP interaction, URL encoding, error responses and loopback connection enforcement. `tests/backend-smoke.js` in the extension folder connects to a live backend and verifies shared sessions, cancellation and event cursor reconnection.

VSIX packaging succeeded and the client smoke passed against xlang3/Uvicorn. VS Code was not found at the usual installation paths on this machine; editor installation and actual UI behavior remain unverified. No Marketplace publication has occurred. Production extension dependencies audit clean; packaging development dependencies reported vulnerabilities and still require dependency review.

## MCP server

The backend exposes workspace tools at `/mcp` using stateless Streamable HTTP, JSON response mode, with protocol versions `2025-11-25`, `2025-06-18`, and `2025-03-26`. It implements initialization, ping, tool discovery and tool invocation. GET/DELETE return 405 because no standalone SSE channel or stateful session is offered. Origin validation and the backend's loopback binding apply. Write permission is the same explicit startup option as local tools.

An independent official TypeScript SDK 1.32.1 Node.js client successfully initialized, discovered tools and read the README from the running xlang3 backend (`Tools/mcp-peer/peer.mjs client URL`). No CPython execution was used for this verification. The outbound xlang3 client also passed HTTP test-double checks for initialization, session headers, pagination, SSE result assembly and response ID validation. Its live Node-peer test failed with an unexpected AnyIO cancellation during TCP connection; that behavior is unresolved and is not a verified outbound integration.

Remote tool configuration uses `AGENTFLOW_MCP_SERVERS`, a JSON list of objects with `name`, `url`, and explicit `allowed_tools` lists. Only listed tools are exposed to agents, under `mcp_SERVER_TOOL` names. Optional `headers` stay in the backend configuration. Do not configure live remote tools until the outbound networking gap is resolved and live interoperability passes.

SDK compatibility gaps: SDK 2.3 import failed because xlang3's cryptography binding lacks `openssl.aead`; SDK 1.26 import failed because Windows `pywintypes` is unavailable under xlang3. `tests/mcp_import_probe.py` records the latter diagnostic and is not a passing acceptance test. The production transport has no dependency on importing the SDK. MCP client, stdio transport, authorization, remote cancellation and broader capability support remain incomplete.

Protocol references: https://modelcontextprotocol.io/specification/2025-11-25/basic/transports and https://modelcontextprotocol.io/specification/2025-11-25/server/tools .
