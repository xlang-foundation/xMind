# Native xMind Server and console client

The current C++ server exposes durable sessions and user messages through the native persistence worker. The console client is a separate process using HTTP. Runtime objects and database files remain owned by the server. This is part of the final backend architecture, with HTTP transport isolated from the agent core.

## Current API

| Method | Path | Behavior |
| --- | --- | --- |
| GET | /v1/health | Reports API, core/storage and execution capability |
| GET / POST | /v1/sessions | List/create durable sessions |
| GET | /v1/sessions/{id}/history | Read conversation history |
| POST | /v1/sessions/{id}/messages | Persist a user message; rejects assistant/system/tool roles |
| GET | /v1/sessions/{id}/runs | Read persisted run history |
| GET | /v1/runs/{id} | Read persisted run state |
| GET | /v1/runs/{id}/events?after=N | Read ordered events after a validated cursor |

There is no product route to fabricate a run or manually advance its state. Creating/running/cancelling agents will be added with the real provider/tool engine. Synthetic run-state fixtures remain in repository tests and the explicitly labeled persistence demo. Health reports `agent_execution: false`; the console currently provides session/message management and run inspection, not a coding assistant.

## Local operation

Build using `Tools/native-milestone.ps1 -Action Build`. Set `XMIND_AUTH_TOKEN` privately in the environment of server and client to the same random 32-256 printable-byte value, with no spaces. It is not a provider credential. Neither executable prints it or accepts it through command arguments. Then:

```powershell
.\build\native\Release\xmind_server.exe --db .agentflow\server.sqlite --modules ..\xlang3\build\Release\modules --stdlib C:\Python\Python314\Lib --port 8765
# In another console with the same authentication environment:
.\build\native\Release\xmind_cli.exe 8765 health
.\build\native\Release\xmind_cli.exe 8765 create-session "My project"
.\build\native\Release\xmind_cli.exe 8765 sessions
.\build\native\Release\xmind_cli.exe 8765 append-message SESSION_ID "My message"
.\build\native\Release\xmind_cli.exe 8765 history SESSION_ID
```

Create the database parent directory first. The server binds only to 127.0.0.1; its configured Host header and bearer token are required. Browser Origin requests are denied until an authorized view adapter is configured. Errors are JSON and backend-internal database diagnostics are not returned. Windows Ctrl+C stops HTTP admission and drains the persistence service. Forced termination uses existing startup interruption recovery on next ownership acquisition.

## Evidence and remaining delivery

`native_http_cli_contract` runs independent Node HTTP peers and the compiled C++ console client against the compiled server. It verifies auth/Host/Origin rejection, duplicate Authorization headers, input validation, duplicate-session conflict, concurrent persisted user messages, independent client visibility and session/message survival after process termination/restart. It also checks that product execution/transition routes are absent and clients cannot insert fabricated assistant messages. Evidence: [native-http-cli-ctest.log](evidence/native-http-cli-ctest.log). The subsequent test also invoked the native PowerShell launcher against the same server: [native-launcher-http-ctest.log](evidence/native-launcher-http-ctest.log).

Run-state/event atomicity and interruption recovery are covered by the separate native repository/worker contracts using synthetic fixtures; the HTTP test does not prove an agent has executed. Native provider/tool execution, cancellation of external effects, streamed events, request deduplication, pagination, credential endpoints, team authorization/TLS, PostgreSQL and VS Code authentication integration remain required. Remote views and WebRTC are not implemented by this loopback service.
