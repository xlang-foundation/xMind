# Native xMind development

Current scope: the [xMind OSS specification](../doc/architecture.md) excludes team-server features, PostgreSQL, WebRTC and the standalone Electron IDE. Those belong to Nexus; earlier roadmap language does not make them OSS completion requirements.

The product launcher uses native C++ targets with embedded xlang3. See [native-server.md](../doc/native-server.md) for the currently verified API and its exact limits. The full target remains the real agent/coding backend, CLI, VS Code, graphs, MCP/A2A and provider support; session persistence is a component, not product completion.

```powershell
.\Tools\agentflow.ps1 -Action Build
# Configure XMIND_AUTH_TOKEN privately for both consoles before starting:
.\Tools\agentflow.ps1 -Action Serve -Port 8765
.\Tools\agentflow.ps1 -Action Client -Port 8765 sessions
.\Tools\agentflow.ps1 -Action Client -Port 8765 create-session 'My project'
```

Use -RuntimeDirectory for the built sibling xlang3 runtime and -PythonLibSource for allowed standard-library source. No CPython interpreter is launched. The xlang3 SQLite prerequisite is recorded in [checkpoint-m1.md](../doc/checkpoint-m1.md).

The complete local native gate now passes 58 contracts, alongside 88 extension and 17 browser checks and an actual native/browser integration contract. See [local evidence](../doc/evidence/native-gemini-local-provenance.json) for its exact source and scope. Synthetic provider/protocol fixtures verify mechanics; they do not establish broad live-provider support or complete feature parity.

The earlier Python/FastAPI prototype, its launcher and dependent probes were removed at the user’s request. Git history retains them. The production runtime is native C++; generic xlang3 dependency probes remain available independently. The [VS Code extension](../extensions/vscode/README.md) now has native API authentication and execution contracts; actual IDE validation remains pending.

The VS Code approval view is implemented with exact payload/before-after inspection and host-mediated allow/deny commands. Eight deterministic client/host tests pass for review binding, stale/duplicate/unreviewed decisions and disposed-view protection. [Evidence](../doc/evidence/vscode-approval-host.log) establishes host behavior and generated script syntax, not actual IDE rendering. Model write-tool integration and uncertainty reconciliation remain pending.
