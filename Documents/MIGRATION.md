# AgentFlow migration baseline

Current direction: native C++ core, with xlang3 integration through its supported native SDK. The Python/FastAPI work described below is historical prototype evidence and no longer the target implementation. See `NATIVE_ARCHITECTURE.md`. OpenCode and LiteLLM remain references only.

## Source revisions

- xMind: `2e637400ada650dd147fb184a8b78c1caafef5be`, cloned into `D:\CantorAI2026\AgentFlow`.
- xlang3: `914783909835116969aad7c66b210a5ac9a27661`, cloned into sibling `D:\CantorAI2026\xlang3`.

## Initial findings

The upstream root CMake build embeds `ThirdParty/xlang`. Core also includes and compiles the old `Api` directory directly. Its entry point loads the old engine through `X::XLoad`, and its agent exports use the old `BEGIN_PACKAGE`/`APISET` interface. Updating a directory path alone cannot establish compatibility with xlang3.

Preserve the upstream implementation as migration reference. Establish the new Python-compatible general agent and backend on xlang3, retaining graph, blueprint, and session concepts. Native integrations must use xlang3's supported SDK rather than assuming the old package ABI is compatible.

The checked-out xlang3 includes native packages for JSON, SQLite, networking, TLS, pydantic_core, httptools, cryptography, and other Python dependency extensions. Their presence is implementation evidence; application compatibility still requires actual import and behavior probes.

## Runtime validation underway

Configured and successfully built xlang3 Release with Visual Studio 18 2026, x64, in `build`. No runtime engine changes have been made.

The following probes passed on this xlang3 executable with `XLANG3_PYTHON_LIB=C:\Python\Python314\Lib` and the project and `.venv/Lib/site-packages` on `PYTHONPATH`:

- `tests/runtime_probe.py`: JSON round trip, SQLite query, asyncio and a FastAPI request.
- `tests/store_probe.py`: persistent messages after reopening the database, active-run conflicts, event cursor replay, terminal-state guards and interrupted-run recovery.
- `tests/server_probe.py`: session/run HTTP APIs, conflicts, missing IDs, cancellation and restart persistence.

Dependencies are pinned in `requirements.lock`. CPython was used to install dependencies and cross-check the initial probes; the application probes above also ran on xlang3.

Runtime gap observed: SQLite constraint errors raise `OperationalError`, and the `IntegrityError` module attribute is absent. AgentFlow exposes a domain-level `RunConflict`, detects the unique-index race through persisted state, and leaves unrelated database failures intact. This does not claim full SQLite exception compatibility.

The API now executes prompted runs through a configurable streaming OpenAI-compatible provider and workspace read/list/write tools. A general agent loop persists conversation and tool results, usage events and progress deltas. Cancellation, per-step timeout and a bounded step count are implemented. An HTTP test double validated streaming tool fragments and model continuation on xlang3; live provider verification remains pending.

Restart recovery marks interrupted runs failed with a resumable marker; actual execution resumption requires the forthcoming agent/checkpoint implementation.

The backend was launched through `Tools/agentflow.ps1` on port 18765 using xlang3/Uvicorn, and separate xlang3 CLI processes successfully created and listed the same persistent session. The smoke server was stopped afterward. Server tests also verify event stream cursor replay for terminal runs.

Start the local backend with `Tools/agentflow.ps1 serve`. It binds to loopback. Authentication must be implemented before expanding access beyond local development.

## Git identity

The user selected `shawn@cantorai.com` for AgentFlow's repository-local commit email. GitHub authentication and destination ownership remain separate from commit authorship. Do not change the global Git identity or xlang3 identity as part of this selection.

## Next evidence required

1. Complete Release build and preserve its executable/runtime libraries before any engine changes.
2. Run JSON, SQLite persistence, async, networking, and FastAPI dependency probes under xlang3.
3. Pin the OpenCode 2 source revision and create a feature acceptance matrix.
4. Implement shared session and run contracts, followed by general agent execution and coding tools.

## Pure-Python dependency setup verified

The application launcher now uses `.agentflow/site-packages`, populated by `Tools/setup.ps1` with pip executed by xlang3. It no longer uses `.venv`. Pure Python packages are pinned in `requirements-pure.lock`; CPython native binaries are excluded. Pydantic-core's Python wrapper sources are extracted from its distribution, while xlang3 provides the native binding. The application package directory scan found zero `.pyd`, `.dll`, `.so`, or `.dylib` files.

Runtime, persistence, backend API, agent execution, MCP server and MCP client test-double probes passed in this clean source-only package environment. Native module `__file__` introspection is unavailable and was not used as proof of its implementation; the source-only package scan and xlang3 build configuration establish the intended loading path.

The official Node MCP SDK client passed against the xlang3 server. The reverse live client probe encountered unexpected AnyIO cancellation while connecting TCP and remains unresolved. No runtime workaround or native fix has been applied; user discussion is required before changing missing native capabilities.
