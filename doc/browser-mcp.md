# Browser automation through MCP

xMind uses its native MCP client to call an external browser controller. The
current integration uses Microsoft's Playwright MCP package. Browser actions
use the same native tool admission, approval, session history and Activity
reporting as other MCP calls. A browser skill can describe a workflow; the MCP
server supplies the executable tools.

The xMind UI continues to run through FastAPI and embedded xlang3 inside
`xmind.exe`. Node.js runs the independent Playwright MCP peer only.

## Local configuration

Install a pinned peer package in a tools directory:

```powershell
npm.cmd install --prefix .agentflow/playwright-mcp --ignore-scripts --no-audit --no-fund --save-exact @playwright/mcp@0.0.83
```

Use the xMind MCP configuration shape in
[the example](examples/mcp-playwright.json), replacing the absolute executable,
package and working-directory paths with backend-local paths. Merge the server
entry with other configured MCP servers. This installation retains `exa-search`
and adds `playwright` in the private `.config/mcp-browser.json` document.

The example uses installed Microsoft Edge in headless mode with an isolated
profile. It does not attach to the user's personal browser.
The explicit Edge executable path avoids browser discovery depending on
environment variables that a native MCP peer may not inherit. Normal MCP workspace
roots and file-access checks remain enabled. Screenshot responses are saved as
artifacts; `--image-responses omit` keeps image base64 out of model tool text.
This mode uses accessibility snapshots for the agent's observations.

Apply a complete configuration through the normal managed profile command:

```powershell
xmind.exe --workspace D:/Projects/Example --profile-root D:/xMindProfiles --mcp-config D:/xMindConfig/mcp.json --upgrade-backend profile-info
```

For a profile serving the embedded web UI, use `xmind.exe web` with its existing
`--workspace`, `--profile-root` and `--assets` plus the new `--mcp-config` and
`--upgrade-backend`. Preserve the profile rather than creating a new database.
The managed handoff checks idle execution and retains the existing web origin.

In Settings, Advanced, MCP servers, `playwright` should appear with the other
configured servers. Start a normal browser task from chat. In Default mode,
review the native MCP operation arguments before allowing each tool call.
Activity identifies the peer tool, server and current state. Do not resend an
already admitted task merely because an observation is slow.

## Reviewing a recorded tool error

An MCP error may follow external side effects, so xMind initially preserves its
quarantine. For a terminal run with an intact, correlated MCP tool-error reply,
Activity offers **Resolve recorded MCP error**. Review the original outcome,
enter an audit note and explicitly acknowledge possible external side effects.
The native transaction retains the original response and failed run, records
the review and releases that operation's quarantine. It never repeats the call.
Lost replies, mismatched receipts and active execution remain blocked.

Upstream references: [Playwright MCP](https://github.com/microsoft/playwright-mcp)
and [configuration](https://playwright.dev/mcp/configuration/options).
