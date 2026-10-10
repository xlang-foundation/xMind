# MCP sign-in from the native console

The `xmind` console calls the same asynchronous C++ sign-in service as browser
and VS Code Settings. It owns observation and user intent, not OAuth execution,
token exchange, credentials or persistence. Use an existing managed local
profile, or `--port PORT` with the normal private `XMIND_AUTH_TOKEN` environment
setting to attach to an explicitly selected backend. Provider keys are not used
by these commands.

| Command | Behavior |
| --- | --- |
| `xmind mcp-auth` | Inspect configured servers' public OAuth status/revisions |
| `xmind mcp-login SERVER` | Read current metadata and start one native attempt |
| `xmind mcp-login-status REQUEST_ID` | Inspect the actual attempt without replay |
| `xmind mcp-login-open REQUEST_ID` | Explicitly open its current HTTPS authorization page on Windows |
| `xmind mcp-login-cancel REQUEST_ID` | Request cancellation; terminal results are observation-only |
| `xmind mcp-login-watch REQUEST_ID` | Emit changed public snapshots as NDJSON until a terminal outcome |

Interactive chat supports `/mcp`, `/mcp-login SERVER`, `/mcp-status REQUEST_ID`,
`/mcp-open REQUEST_ID`, `/mcp-cancel REQUEST_ID` and `/mcp-watch REQUEST_ID`.
These commands do not require an enabled model or start an agent conversation.

Start validates the observed enabled/configured server and selects its current
configuration/credential revisions automatically. It generates a native random
request ID, flushes that identity to standard error before admission, and sends
the exact server/request/revision fields. A missing or malformed reply is not
retried. Use the published request ID with `mcp-login-status` to inspect the
outcome; starting another login is an explicit new command.

Opening a page requires an observed callback-wait phase, no requested
cancellation, a future actual expiry and a validated HTTPS URL. The Windows
launcher uses the HTTPS URI directly with `ShellExecuteW`; it never builds a
shell command or accepts an arbitrary URL argument. Unsupported platforms
report that opening a page is unavailable. CLI status still returns its
validated public authorization URL.

Cancellation first validates the actual attempt. A terminal outcome returns
without a POST; a concurrent committed grant remains a connected outcome.
Watching binds the request, server, configuration and original credential
revision across snapshots. A successful terminal grant returns zero; other
terminal outcomes return one. A six-minute observation deadline detaches and
does not cancel native execution. Exiting chat or terminating the console also
detaches. None of these commands retries a model request or MCP tool effect.

Metadata parsing is bounded and rejects duplicate fields, extra/private fields,
unsafe integers, foreign ownership, unknown states/reasons and non-HTTPS,
credential-bearing or control-bearing links. Rejected response bodies are not
printed. Configuration and access/refresh tokens stay in the backend.

The independent synthetic HTTP contract exercises the compiled console's
command and validation boundary. A separate actual native-service peer exercises
console setup/status/watch/cancel against real embedded-xlang3 encrypted storage
and an untrusted HTTPS peer. Those checks do not establish a successful OAuth
login or a real browser launch. Trusted HTTPS positive login, registration
selection, refresh/revocation and running-owner coordination remain required.
The complete **114/114** local native gate passes in **388.18 seconds**, with
**2455** unchanged mapped inputs, plus **257 extension / 55 browser** checks.
See [the shared native service](native-mcp-login.md),
[exact checkpoint evidence](evidence/native-oauth-cli-local.json) and
[the separately retained prior hosted manifest failure](evidence/native-mcp-login-view-ci-f595205.json).
