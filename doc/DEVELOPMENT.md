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

Current source passed the complete local **66 native contracts in 114.49
seconds**, **98 extension and 17 browser tests**, with its exact native manifest,
zero failures/skips and no post-build exclusions. The new actual Claude
AgentRunner fixture passed in **0.56 seconds**, and its provider adapter contract
in **0.23 seconds**. Real native file-tool execution, correlated Messages
results, encrypted credentials and xlang3 SQLite reopen/history replay,
protocol failures before effects, held-stream cancellation/recovery and actual
SQL conversation-batch rollback passed with an independent synthetic provider
peer. Native usage and the shared DOM renderer preserve uncached input, separate
cache-write/read counters, zeros, missing counts and measured response timing
without derived totals. Fresh browser/native integration also passed with the
rebuilt server and source-matched assets; installed previews remain unchanged.
[Exact local Claude scope](evidence/native-anthropic-agent-local-provenance.json).
Hosted verification of this 66-contract source remains pending. These checks do
not establish Claude thinking/signatures, live account acceptance or actual
rendered IDE acceptance. See [Claude agent validation](native-claude-agent.md).

The earlier checkpoint `ac69c1f` separately passed the complete local **65 native
contracts in 113.16 seconds**, with the exact manifest, zero failures/skips and no post-build
exclusions. Its new provider-profile CLI contract passed in **4.23 seconds**;
fresh browser/native integration also passed against the rebuilt server and
source-matched assets. [Exact local scope](evidence/native-provider-profile-cli-local-provenance.json).
Its [hosted run 37746664258](https://github.com/xlang-foundation/xMind/actions/runs/37746664258)
was last observed running; no hosted success is claimed here. That prior gate
excludes the newer Claude agent and renderer work above.

The preceding enrollment revision `2f5e0f0` separately passed its exact hosted
**64 native contracts in 158.09 seconds**, **94 extension and 17 browser tests**,
native/browser integration and VSIX verification, with zero failures/skips.
[Hosted enrollment evidence](evidence/native-gemini-enrollment-hosted-provenance.json).
For the earlier CLI checkpoint, unchanged frontend sources retained that prior
hosted result; those frontend suites were not rerun or counted as new results
for the CLI change. Provider
peers and keys are labelled synthetic. No live Gemini inference, rendered IDE
enrollment, installed-preview upgrade or complete feature parity is claimed.
See [current validation](VALIDATION_STATUS.md) for the full acceptance boundaries.

The earlier Python/FastAPI prototype, its launcher and dependent probes were removed at the user’s request. Git history retains them. The production runtime is native C++; generic xlang3 dependency probes remain available independently. The [VS Code extension](../extensions/vscode/README.md) uses the native API authentication and execution contracts; its guide records the specific adapter and actual-IDE acceptance scopes.

The VS Code approval view uses exact payload/before-after inspection and
host-mediated allow/deny commands. Its initial approval-host checkpoint passed
**eight deterministic client/host tests** for review binding,
stale/duplicate/unreviewed decisions and disposed-view protection.
[Historical evidence](evidence/vscode-approval-host.log) records fixture-based
host behavior and generated script syntax, without establishing actual IDE
rendering. For native model-write/effect/recovery validation and the remaining
editor acceptance boundaries, see [current validation](VALIDATION_STATUS.md)
and the [VS Code guide](../extensions/vscode/README.md).
