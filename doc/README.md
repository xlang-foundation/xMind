# xMind documentation

Start with the [OSS architecture](architecture.md) and its [SVG](architecture.svg).
xMind uses native C++ with embedded xlang3 and local SQLite. Its clients are the
CLI, browser and VS Code. Private Nexus owns the team server, PostgreSQL,
WebRTC and Electron IDE.

| Topic | Maintained guide |
| --- | --- |
| Build and development | [Development](DEVELOPMENT.md), [isolated CI](native-ci.md) |
| Server and console | [Native server](native-server.md), [interactive CLI](native-interactive-cli.md) |
| Browser and editor | [Browser view](browser-view.md), [VS Code setup](../extensions/vscode/README.md) |
| Models and credentials | [Provider setup](provider-setup.md), [credential storage](credential-storage.md) |
| General agents and graphs | [Agent loop](native-agent-loop.md), [delegation](native-delegation.md), [graph service](native-graph-service.md) |
| Coding tools and approvals | [Workspace tools](native-workspace-tools.md), [file creation](native-file-creation.md), [process tools](native-process-tools.md), [permissions](PERMISSIONS.md) |
| Protocols and profiles | [MCP](native-mcp.md), [A2A](A2A.md), [connection profiles](connection-profiles.md) |
| Coverage and verification | [Validation status](VALIDATION_STATUS.md), [parity baseline](PARITY.md), [model coverage](MODEL_SUPPORT.md), [milestones](milestones.md) |
| Source layout | [Cleanup](cleanup.md) |

Each verification record identifies its source revision and scope. Historical
checkpoints and `evidence/` preserve actual results; they do not establish that
unfinished source or later changes have passed. The [dynamic planning
design](native-dynamic-plan-design.md) remains separate from shipped delegation.
