# Historical Python prototype

This directory contains the earlier AgentFlow Python/FastAPI prototype. It is
retained as historical material and for its existing prototype tests. It is
not the production xMind agent runtime, model gateway, MCP or A2A implementation.

The current backend and CLI are the C++ targets under `../Native/`, including
`xmind_server.exe` and `xmind_cli.exe`. Browser and VS Code clients use their
native command/event APIs. Embedded xlang3 runs scripts and performs SQLite I/O
behind native repository contracts; that does not make this prototype package
part of the native execution path.

| Prototype files | Original responsibility |
| --- | --- |
| `engine.py`, `graph.py` | Agent and graph execution |
| `providers.py` | OpenAI-compatible model requests and streaming |
| `mcp.py`, `mcp_client.py` | MCP server/client adapters |
| `a2a.py`, `a2a_client.py` | A2A server/client adapters |
| `server.py`, `main.py` | FastAPI API and console entry point |
| `store.py`, `permissions.py`, `tools.py` | Persistence, approvals and workspace tools |

Do not launch `agentflow.main` as the current product backend or use these
modules as evidence of native feature completion. Follow
[the current architecture](../doc/architecture.md) for production scope and
the native implementation's documented verification limits.
