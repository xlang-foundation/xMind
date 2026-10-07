# Native xMind development

The product launcher uses native C++ targets with embedded xlang3. See [native-server.md](../doc/native-server.md) for the currently verified API and its exact limits. The full target remains the real agent/coding backend, CLI, VS Code, graphs, MCP/A2A and provider support; session persistence is a component, not product completion.

```powershell
.\Tools\agentflow.ps1 -Action Build
# Configure XMIND_AUTH_TOKEN privately for both consoles before starting:
.\Tools\agentflow.ps1 -Action Serve -Port 8765
.\Tools\agentflow.ps1 -Action Client -Port 8765 sessions
.\Tools\agentflow.ps1 -Action Client -Port 8765 create-session 'My project'
```

Use -RuntimeDirectory for the built sibling xlang3 runtime and -PythonLibSource for allowed standard-library source. No CPython interpreter is launched. The xlang3 SQLite prerequisite is recorded in [checkpoint-m1.md](../doc/checkpoint-m1.md).

The native build runs the repository, worker, credential and independent HTTP/CLI contracts. Synthetic fixtures establish component invariants only. Prompt execution and manual run-state mutation are absent from the product API until implemented by the real engine. No live provider, coding workflow, protocol peer, graph or editor completion is claimed by these tests.

Historical Python/FastAPI scaffolding remains under agentflow/ for audit/reference, with its explicit launcher Tools/prototype-agentflow.ps1 and [historical development notes](PROTOTYPE_DEVELOPMENT.md). It is excluded from the product launcher and native build. The existing VS Code extension still needs authentication and native agent integration before it can serve the final backend.