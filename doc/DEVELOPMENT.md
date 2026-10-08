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

Current source adds direct MCP nodes to model-free registered graphs. Enroll a
trusted stdio configuration while the backend is stopped, use native
`discover-mcp` to discover its public alias and revision, then pin both in the
catalogue. MCP nodes use a strict `arguments_json` string so literal numeric
tokens and escaped keys survive catalogue/checkpoint storage. Execution uses
fresh discovery, the shared native schema/approval/journal path and the root
segment deadline. Metadata and admission never launch peers. Stale paused
graphs remain inspectable/cancellable, with resume rejected before input commit.
[Configuration, arguments and limits](native-graph-mcp.md).

The final complete local **69-contract native gate passed in 121.22 seconds**,
with the exact expected/registered/passed manifest, zero failures/skips and no
post-build exclusions. All **14 frozen source hashes** match the final pre-build
state. The new native graph/permission contract took **5.59 seconds**; actual
administrator/server/CLI/shared-view-adapter integration took **2.31 seconds**.
Real approved file effects, pause/reopen, native dependent reads, rejected and
uncertain paths, serialized shared-server claims and journal fail-stop/recovery
passed using synthetic peer protocol replies. Fresh browser/native integration
also passed against the rebuilt server and source-matched assets. The first
HTTP startup guard failure is retained in a [separate interim log](evidence/native-graph-mcp-initial-ctest.log);
the final gate includes the corrected guard and expanded invalid model settings.
Exact revision `c4ec09fc3ee25c1b3a2bd9087b25f5af420ee616` subsequently passed
its [hosted gate](https://github.com/xlang-foundation/xMind/actions/runs/37755382937):
**69 native contracts in 184.36 seconds**, **98 extension tests in 3.2107402
seconds** and **17 browser tests in 1.5543002 seconds**, plus native/browser
integration and verification of the 18-asset VSIX. All 16 job steps passed;
there were zero failures/skips and the native manifest matched exactly.
Live direct-MCP graph and rendered IDE acceptance remain separate requirements.
[Exact local69 scope](evidence/native-graph-mcp-local-provenance.json),
[exact hosted69 scope](evidence/native-graph-mcp-hosted-provenance.json).

The next [native delegation design](native-delegation-design.md) describes
model-selected leaf investigations in ordinary Agent mode, with actual child
AgentRunners, shared budgets, owned histories and controller-approved parent
effects. It is a design; delegation is not implemented by this checkpoint.

The managed browser preview now runs the verified c4 native bundle and packaged
view. Prior records/configuration were preserved and an actual read-only
OpenAI Agent request displayed retained token/timing metrics after refresh.
The original browser session required a reconnect; its precise cause remains
unresolved. [Installed scope and screenshot](preview-checkpoint-c4ec09fc.md).

The preceding `fceb50b` source passed the complete local **67 native contracts in 117.65
seconds**, with the exact native manifest, zero failures/skips and no post-build
exclusions. The new Claude history contract passed in **0.50 seconds**, and the
expanded actual AgentRunner contract in **0.57 seconds**. Ordered signed and
redacted blocks surround real native file-tool calls; `content_json` receipts
retain original tool-input whitespace, escaped keys and exact number tokens
through xlang3 SQLite reopen and outgoing Messages replay without repeating
earlier tools. Unsigned thinking fails before effects. Hidden content remains
private continuation material, without ordinary text output or local
cryptographic signature verification.

Native receipt/request bounds, ordered DTO matching, same-wire provenance,
request counts and aggregate/role-merge budgets passed. Content/envelope and
final request limits are 8 MiB each, content arrays permit 64 blocks, tool input
is limited to 1 MiB/depth 16, and signatures to 65,536 bytes. Foreign,
mismatched and empty outgoing receipts reject before transport. Fresh
browser/native integration passed the rebuilt server and matching assets.
Frontend sources remain unchanged from `ae7c4ba`, retaining its prior **98
extension and 17 browser tests**; these suites were not repeated for the native
receipt change. [Exact local67 scope](evidence/native-anthropic-history-local-provenance.json),
[receipt implementation and bounds](native-claude-history.md).
Exact revision `fceb50ba0493f645ee5f0a00d5400e0f103df231` subsequently passed
its [hosted gate](https://github.com/xlang-foundation/xMind/actions/runs/37752689263):
**67 native contracts in 120.18 seconds**, **98 extension tests in 2.0700124
seconds** and **17 browser tests in 0.9230928 seconds**, native/browser integration
and verification of an 18-asset VSIX, with zero failures/skips and the exact
manifest. Hosted history/agent contracts took **0.31/1.03 seconds**. This result
excludes the newer direct MCP graph, administrator and server startup changes.
[Exact hosted67 scope](evidence/native-anthropic-history-hosted-provenance.json).
Provider replies/keys/signatures are synthetic. Thinking/reasoning request
controls, live Claude acceptance, foreign signed/tool
history conversion, actual rendered IDE acceptance and preview upgrades remain
incomplete.

The preceding `ae7c4ba` source passed the complete local **66 native contracts in 114.49
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
Exact revision `ae7c4ba07d59f57cbe393eff5a01b7d5186dfbc5` subsequently passed
its [hosted gate](https://github.com/xlang-foundation/xMind/actions/runs/37748603655):
**66 native contracts in 131.92 seconds**, **98 extension and 17 browser tests**,
native/browser integration and VSIX verification, with zero failures/skips and
an exact manifest. Its Claude agent contract passed in **1.41 seconds**. Both
prior gates exclude the newer signed/redacted receipt work above.
[Hosted66 evidence](evidence/native-anthropic-agent-hosted-provenance.json),
[Claude agent validation](native-claude-agent.md).

The earlier checkpoint `ac69c1f` separately passed the complete local **65 native
contracts in 113.16 seconds**, with the exact manifest, zero failures/skips and no post-build
exclusions. Its new provider-profile CLI contract passed in **4.23 seconds**;
fresh browser/native integration also passed against the rebuilt server and
source-matched assets. [Exact local scope](evidence/native-provider-profile-cli-local-provenance.json).
Exact revision `ac69c1f2a2c4a757b23d0d9da8b98d6188fab3a8` subsequently passed
its [hosted gate 37746664258](https://github.com/xlang-foundation/xMind/actions/runs/37746664258):
**65 native contracts in 149.37 seconds**, **94 extension and 17 browser tests**,
native/browser integration and VSIX verification, with zero failures/skips and
an exact manifest. This result excludes both newer Claude milestones above.
[Hosted CLI evidence](evidence/native-provider-profile-cli-hosted-provenance.json).

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
