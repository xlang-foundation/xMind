# Native xMind development

Current scope: the [xMind OSS specification](../doc/architecture.md) excludes team-server features, PostgreSQL, WebRTC and the standalone Electron IDE. Those belong to Nexus; earlier roadmap language does not make them OSS completion requirements.

The product launcher uses native C++ targets with embedded xlang3. See [native-server.md](../doc/native-server.md) for the currently verified API and its exact limits. The full target remains the real agent/coding backend, CLI, VS Code, graphs, MCP/A2A and provider support; session persistence is a component, not product completion.

Native Responses reasoning continuation passed the exact ace2460 hosted gate: **71 native contracts in 171.52 seconds, 103 extension and 20 browser contracts**, with all 16 job steps, native/browser integration and 18 VSIX assets passing. Verified sources and artifact maps bind the completed-item continuation fix and its real native two-child read/join with synthetic provider replies. The local build was deferred by the benchmark guard. The exact native bundle is installed. One real OpenAI browser prompt completed two read-only native children and a joined parent response, with six owned model attempts, separate per-response metrics and zero effect operations. Immediate refresh retained the connection and selection. Upgrade and rollback checks passed at their separately documented scopes. [Compatibility and scope](native-responses-reasoning.md), [exact hosted evidence](evidence/native-responses-reasoning-hosted-provenance.json).

```powershell
.\Tools\agentflow.ps1 -Action Build
# Configure XMIND_AUTH_TOKEN privately for both consoles before starting:
.\Tools\agentflow.ps1 -Action Serve -Port 8765
.\Tools\agentflow.ps1 -Action Client -Port 8765 sessions
.\Tools\agentflow.ps1 -Action Client -Port 8765 create-session 'My project'
```

Use -RuntimeDirectory for the built sibling xlang3 runtime and -PythonLibSource for allowed standard-library source. No CPython interpreter is launched. The xlang3 SQLite prerequisite is recorded in [checkpoint-m1.md](../doc/checkpoint-m1.md).

Current source also adds [ordinary-Agent delegation](native-delegation.md).
Ordinary Agent mode now offers native `delegate_tasks` for model-selected read-only workspace investigations. Actual child AgentRunners keep separate conversations and measured response usage/timing, share their parent's call/deadline limits, and return observed results before the parent continues or requests an approval-controlled effect. Schema v10 durably binds tasks, immutable presets, budgets and once-only settlements; recovery never replays children. CLI, browser and VS Code observe the same owned child histories and committed tree cursors.

For the preceding delegation checkpoint, the final guarded local **71-contract native gate passed in 142.36 seconds**, with an exact expected/registered/passed manifest, **45 frozen source hashes**, zero failures/skips and no exclusions. The delegation engine contract took **1.96 seconds**, actual HTTP/CLI/shared-controller acceptance **1.03 seconds**. **103 extension tests** and **19 browser tests**, fresh source-matched browser/native integration and 18-asset VSIX verification also passed. Provider replies, signatures and keys are explicitly synthetic; native agents, filesystem effects, permissions, xlang3 SQLite and transport are real. The [initial process-fixture positional-schema failure](evidence/native-delegation-initial-ctest.log) is retained separately; the corrected frozen source passed all 71 contracts.

Exact revision `e353a37799530a234a6fa13e51f61a5c52d3ae6a` subsequently passed its [hosted gate](https://github.com/xlang-foundation/xMind/actions/runs/37765666399): **71 native contracts in 175.51 seconds**, **103 extension tests in 2.9668103 seconds** and **19 browser tests in 1.426115 seconds**, plus native/browser integration and verification of all 18 required VSIX assets. The delegation engine took **4.88 seconds** and its HTTP/CLI contract **1.22 seconds**. All 16 job steps succeeded, with zero failures/skips and the exact complete native manifest. Downloaded artifact digests, exact source and runtime/stdlib pins were verified. [Exact hosted71 provenance](evidence/native-delegation-hosted-provenance.json), [raw hosted CTest](evidence/native-delegation-hosted-ctest.log), [original hosted job](evidence/native-delegation-passing-ci-job.log).

The preceding managed backend upgrade installed the exact hosted e353 bundle with schema v10; the later 7fe upgrade is recorded in the diagnostic scope. The actual upgrade preserved existing records, settings and browser access; its disposable schema 9→10→9 rollback fixture passed separately. The original e353 webpage exposed a classic-script name collision. A separate local browser repair passed **103 extension tests in 1.6557025 seconds**, **20 browser tests in 0.9090882 seconds**, native/browser integration and all 18 VSIX assets. The repaired page reused its cookie without key entry and restored prior history/metrics, the selected model and Agent mode. These frontend results are separate from the unchanged hosted 71/103/19 gate. [Upgrade evidence](evidence/native-delegation-upgrade-provenance.json), [acceptance scopes](native-delegation-acceptance.md).

The first e353 live `delegate_tasks` request failed with `responses_terminal_mismatch` before child admission: zero children, only the user history row and one parent model-budget attempt. That historical failure remains recorded; the later ace browser two-child join passed the separate acceptance above. Installed VS Code delegation acceptance remains unverified. Mutable dependency planning, skills, compaction, outbound A2A and full provider/coding parity remain incomplete. [Implementation and limits](native-delegation.md), [next native planning design](native-dynamic-plan-design.md).

The preceding direct-MCP checkpoint adds direct MCP nodes to model-free registered graphs. Enroll a
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

The [delegation design](native-delegation-design.md) retains the broader target
beyond the initial locally and hosted-verified read-only leaves. A new v10 native
bundle requires its complete native gate and source/artifact verification;
the managed preview uses the later verified 7fe diagnostic bundle.
[Current installation](native-responses-diagnostics.md).

The preceding c4 managed preview preserved prior records/configuration and an actual read-only
OpenAI Agent request displayed retained token/timing metrics after refresh.
The original browser session required a reconnect; its precise cause remains
unresolved. [Historical scope and screenshot](preview-checkpoint-c4ec09fc.md).
The preceding e353 backend upgrade, separate browser repair and first failed live
delegation request are recorded in [current acceptance](native-delegation-acceptance.md).

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
