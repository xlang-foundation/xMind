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

The native build runs eighteen contracts covering persistence, credentials, filesystem tools/snapshots/edit planning, backend in-place application and approved edit execution, provider transport, the real native model/tool loop, worker shutdown, operation authorization and HTTP/CLI execution. Local approval inspection/decision routes are exercised through the actual CLI and VS Code host client against real effects. An outcome-storage fault after actual file application verifies uncertain recovery without replay. Synthetic inference peers establish protocol/component invariants only. With an explicit model configuration, the server accepts prompts and cancellation through the actual native engine. Manual run-state mutation is absent. Agent write-tool integration, approval UI, partial-write/process-kill tests and reconciliation remain pending. No live-provider, coding-workflow, graph/protocol or editor-UI completion is claimed by these tests.

Historical Python/FastAPI scaffolding remains under agentflow/ for audit/reference, with its explicit launcher Tools/prototype-agentflow.ps1 and [historical development notes](PROTOTYPE_DEVELOPMENT.md). It is excluded from the product launcher and native build. The [VS Code extension](../extensions/vscode/README.md) now has native API authentication and execution contracts; actual IDE validation remains pending.
