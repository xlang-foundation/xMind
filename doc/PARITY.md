# OpenCode 2 feature baseline

The [xMind OSS specification](architecture.md) covers the local C++/xlang3
backend, SQLite, single and graph agents, native providers, MCP/A2A, CLI,
browser UI and the VS Code extension. Local/Nexus connection profiles use the
shared command/event protocol. Nexus owns the private team server, PostgreSQL,
WebRTC and standalone Electron IDE; these are excluded from OSS delivery.

OpenCode is a behavior reference only. The Python/FastAPI agent prototype and
original xlang service/plugin tree were removed at the user's request. Git
history preserves them, but prototype checks and source mappings do not count
as native delivery. See [native ownership](NATIVE_ARCHITECTURE.md) and
[cleanup](cleanup.md).

Baseline: `anomalyco/opencode` tag `v2.0.16`, commit
`3a103fe0aff726a4edc7492f03f7b88195d9e4c9`, verified with `git ls-remote`.
[Pinned source](https://github.com/anomalyco/opencode/tree/v2.0.16),
[documentation](https://opencode.ai/v2/docs). Documentation can differ from this
release; inspect the pinned source before finalizing each acceptance criterion.

## Current native verification scope

Native workspace skill discovery, loading, context delivery and effect approval
bindings passed the complete 90-contract hosted native gate, browser/native
integration and packaging at source `d767d506`. Independent verification binds
the packaged runtime and view sources to that commit. That package is installed
and passed an actual VS Code opened-folder host plus four real-provider skill
activation/companion reads. Rendered acceptance and broader skill delivery
remain pending; this is not a verified equivalent
OpenCode skill operation. [Skill scope and remaining delivery](native-skills.md).
The shared skill approval display passed the complete isolated 176-extension /
35-browser view gate. It displays source bindings for file, command and MCP
approvals; full skill delivery and rendered acceptance remain
separate requirements. [Verified view evidence](evidence/native-skill-approval-views-hosted.json).
The [separate installed/live receipt](evidence/native-skills-installed-live.json)
retains the initial Claude formatting and harness cleanup failures and the
successful Claude retry. The browser preview subsequently upgraded to the same
accepted runtime with exact saved-record comparison. Reload and real skill
metadata rendering passed, but a moving approval-card interaction failed and
was cancelled before an effect. The DOM-retention fix subsequently passed its
complete isolated 177/35 gate and is published in the browser preview. A real
retry passed expanded-guidance retention, sidebar approval, native file creation,
exact final response and reload with actual metrics. The fixed package at
`5d48bdd72713dcc23d5a1b15a0a08d3e3b7b1fd2` subsequently passed its complete
90-contract native workflow and replaced the normal VS Code profile's installed
package. Its full runtime and view inventory was verified with unchanged
settings. Ordinary-window reload and rendered acceptance remain pending;
full skills remain incomplete.
[Package and installation evidence](evidence/native-approval-stability-hosted-package.json).
The subsequent schema13 source passed the exact full 91-contract local native
gate and real OpenAI continued-session restoration after backend exit/restart,
including reading a changed guide's new companion without reloading the skill.
Selections commit atomically with native tool turns and remain bound to the
session/workspace or child run. Initial constructor-check and live harness
configuration failures are retained in
[local persistence evidence](evidence/native-skill-state-local.json).
Hosted packaging and installed/rendered schema13 acceptance remain pending.
[Browser evidence](evidence/native-skills-browser-upgrade.json).
[Verified view gate](evidence/native-approval-stability-views-hosted.json),
[actual approval retry](evidence/native-approval-stability-live.json).

The later catalogue-recovery source passed its isolated hosted **175/35** view
gate and its tested browser asset is published. Rendered recovery and an updated
VSIX remain pending; the local benchmark guard has deferred further local
validation. This fixes an adapter recovery path without claiming a new native
gate or broader behavioral parity.
[Catalogue recovery](model-catalogue-recovery.md).

The earlier browser preview source `60475f84` passed its full 89-contract gate and real
four-provider native workspace reads. Its installed browser upgrade preserved
existing API records and passed actual four-provider rendered runs and refresh.
The later compact-height CSS passed 173/33 frontend tests and actual short-pane
rendering. These checkpoints do not establish full parity: rendered normal
VS Code interaction, concurrent provider/session catalogue recovery, live
compaction and broader provider/coding capabilities remain separate work.
[Model policy](native-model-eligibility.md),
[browser upgrade](browser-preview-upgrade.md),
[compact sidebar](compact-sidebar.md).
The older acceptance paragraphs below retain their original checkpoint scope.

Current schema-v12 context, YAML configuration and DeepSeek source configured
and compiled in the second complete **88-contract** attempt, which passed
**85 and failed three in 162.98 seconds**. The first 86-contract attempt passed
81 and failed five. Both failures are retained. After repository/controller and
CLI fixture repairs, the third full gate passed **88/88 in 161.48 seconds**,
with zero failures/skips, all 560 frozen inputs unchanged and original test
output captured. DeepSeek gateway/profile
contracts passed individually in **0.54 / 0.71 seconds** with synthetic replies.
The earlier **131 extension / 29 browser** source tests retain their scope.
The final saved-provider footer passed **141 extension / 32 browser** tests,
with all 37 frozen inputs unchanged. It loads catalogues using saved credentials
and keeps the provider and model controls together at the bottom of the sidebar.
[Frontend source evidence](evidence/native-provider-footer-ui-provenance.json)
does not establish installation or live-provider acceptance. The later
model-free native/browser integration passed in **3.30 seconds** and the
actual VSIX passed **18 asset checks**, with all 12 tested view files matching.
[Integration/package evidence](evidence/native-provider-footer-integration-provenance.json)
has its own scope and retains the initial guard-host failure.
The complete local pass establishes its contract scope. A later Responses
serializer defect was repaired and passed the fourth complete **88/88 gate in
171.89 seconds**, with all 560 source inputs unchanged and no failures/skips.
[Exact local evidence](evidence/native-context-provider-local-provenance.json)
binds that repair and the original output;
installed/live context and broader provider strategy acceptance remain separate.
[Current implementation](native-context-compaction-design.md),
[provider scopes](provider-setup.md).

All four provider accounts passed separate direct HTTPS catalogue and small
generation checks. The installed ace/schema-v10 preview still has one configured
OpenAI Responses profile and four advertised routes, with DeepSeek absent; the
local four-key configuration has not been imported. An earlier run returned HTTP
400 `invalid_value`; an isolated direct reproduction later identified legacy
assistant `input_text` instead of `output_text` in both ace and current source.
A fresh native OpenAI browser prompt separately returned `OK` with supplied
usage **3884 / 5**. That success does not validate legacy-history replay or
four-provider native coding; direct account smoke checks remain separate.

The native [JSON POST transport foundation](native-context-transport.md)
passed its complete local **78-contract gate in 193.91 seconds**, with 427
frozen inputs and the accepted xlang3 runtime verified unchanged. It supplies
bounded transport; the later compaction and token-counting implementation has
the separate local scope above. Exact `859aca7` subsequently passed its separate
hosted **78 native / 119 extension / 26 browser** gate, all 16 steps, model-free
integration and 18 VSIX assets. That transport checkpoint contains no compaction
acceptance. [Local evidence](evidence/native-context-transport-local-provenance.json),
[hosted transport evidence](evidence/native-context-transport-hosted-859-provenance.json).

Earlier exact source `2cd392f` passed the [hosted MCP gate](https://github.com/xlang-foundation/xMind/actions/runs/37824215372):
**78 native / 119 extension / 26 browser** tests, all 16 steps, model-free
integration and 18 VSIX assets. Exact Git, raw logs, archive digests and
payload maps were verified; the installed preview remains ace/schema v10.
[Hosted evidence](evidence/native-dynamic-mcp-hosted-2cd-provenance.json).

The bounded dependency-planning implementation now has a complete local
**78-contract gate** covering actual MCP effects inside dynamically admitted
coding children. Five new integration scenarios verify exact child-owned
approval and captured protocol bindings, a real peer write with a separate
native verification read, denial/cancellation before dispatch, and lost-reply
uncertainty without reopening replay. The provider replies and bespoke legacy
peer descriptors are synthetic; native agents, effects and xlang3 SQLite are
real. The initial malformed fixture failure remains separate. Frontend source
and the installed ace/schema-v10 preview are unchanged. This does not establish
live planning, editor acceptance, recursive planning, deterministic dynamic
nodes, skills, compaction, outbound A2A or complete coding/provider parity.
[Local78 evidence](evidence/native-dynamic-mcp-local-provenance.json),
[planning scope](native-dynamic-plan.md).

Native Responses reasoning continuation passed the exact ace2460 hosted gate: **71 native contracts in 171.52 seconds, 103 extension and 20 browser contracts**, with all 16 job steps, native/browser integration and 18 VSIX assets passing. Verified sources and artifact maps bind the completed-item continuation fix and its real native two-child read/join with synthetic provider replies. The local build was deferred by the benchmark guard. The exact native bundle is installed. One real OpenAI browser prompt completed two read-only native children and a joined parent response, with six owned model attempts, separate per-response metrics and zero effect operations. Immediate refresh retained the connection and selection. Upgrade and rollback checks passed at their separately documented scopes. [Compatibility and scope](native-responses-reasoning.md), [exact hosted evidence](evidence/native-responses-reasoning-hosted-provenance.json).

Ordinary Agent mode now offers native `delegate_tasks` for model-selected read-only workspace investigations. Actual child AgentRunners keep separate conversations and measured response usage/timing, share their parent's call/deadline limits, and return observed results before the parent continues or requests an approval-controlled effect. Schema v10 durably binds tasks, immutable presets, budgets and once-only settlements; recovery never replays children. CLI, browser and VS Code observe the same owned child histories and committed tree cursors.

For the preceding delegation checkpoint, the final guarded local **71-contract native gate passed in 142.36 seconds**, with an exact expected/registered/passed manifest, **45 frozen source hashes**, zero failures/skips and no exclusions. The delegation engine contract took **1.96 seconds**, actual HTTP/CLI/shared-controller acceptance **1.03 seconds**. **103 extension tests** and **19 browser tests**, fresh source-matched browser/native integration and 18-asset VSIX verification also passed. Provider replies, signatures and keys are explicitly synthetic; native agents, filesystem effects, permissions, xlang3 SQLite and transport are real. The [initial process-fixture positional-schema failure](evidence/native-delegation-initial-ctest.log) is retained separately; the corrected frozen source passed all 71 contracts.

Exact revision `e353a37799530a234a6fa13e51f61a5c52d3ae6a` subsequently passed its [hosted gate](https://github.com/xlang-foundation/xMind/actions/runs/37765666399): **71 native contracts in 175.51 seconds**, **103 extension tests in 2.9668103 seconds** and **19 browser tests in 1.426115 seconds**, plus native/browser integration and verification of all 18 required VSIX assets. The delegation engine took **4.88 seconds** and its HTTP/CLI contract **1.22 seconds**. All 16 job steps succeeded, with zero failures/skips and the exact complete native manifest. Downloaded artifact digests, exact source and runtime/stdlib pins were verified. [Exact hosted71 provenance](evidence/native-delegation-hosted-provenance.json), [raw hosted CTest](evidence/native-delegation-hosted-ctest.log), [original hosted job](evidence/native-delegation-passing-ci-job.log).

The preceding managed backend upgrade installed the exact hosted e353 bundle with schema v10; the later 7fe upgrade is recorded in the diagnostic scope. The actual upgrade preserved existing records, settings and browser access; its disposable schema 9→10→9 rollback fixture passed separately. The original e353 webpage exposed a classic-script name collision. A separate local browser repair passed **103 extension tests in 1.6557025 seconds**, **20 browser tests in 0.9090882 seconds**, native/browser integration and all 18 VSIX assets. The repaired page reused its cookie without key entry and restored prior history/metrics, the selected model and Agent mode. These frontend results are separate from the unchanged hosted 71/103/19 gate. [Upgrade evidence](evidence/native-delegation-upgrade-provenance.json), [acceptance scopes](native-delegation-acceptance.md).

The first e353 live `delegate_tasks` request failed with `responses_terminal_mismatch` before child admission: zero children, only the user history row and one parent model-budget attempt. That historical failure remains recorded; the later ace browser two-child join passed the separate acceptance above. Installed VS Code delegation acceptance remains unverified. Mutable dependency planning, skills, compaction, outbound A2A and full provider/coding parity remain incomplete. [Implementation and limits](native-delegation.md), [next native planning design](native-dynamic-plan-design.md).

New source adds direct MCP graph tool nodes and explicit offline native alias
discovery. Nodes bind an opaque alias to trusted server ID/revision, preserve
raw literal arguments inside a string, and rediscover that exact alias before
using the existing durable approval/effect journal. Graphs can run without a
model; metadata/admission do not start peers. Changed paused bindings remain
inspectable/cancellable and cannot resume against another revision. Oversized
acknowledged graph output fails the child while preserving the succeeded
operation. The final guarded **69 native contracts passed in 121.22 seconds**,
with the exact expected/registered/passed manifest, zero failures/skips and no
post-build exclusions. The graph MCP contract took **5.59 seconds** and actual
HTTP/admin/CLI/view-adapter contract **2.31 seconds**. They verify real approved
external writes/dependent reads, raw arguments and xlang3 SQLite reopen,
denial/cancellation, descriptor drift, stale pauses, acknowledged output bounds,
reply-loss quarantine and journal fail-stop without replay. Fourteen frozen
source hashes match the pre-build capture. Fresh browser/native integration
passed; unchanged frontend sources retain exact fceb hosted **98 extension and
17 browser tests**, without rerunning those suites for this native change.
Exact source `c4ec09fc3ee25c1b3a2bd9087b25f5af420ee616` subsequently passed
hosted **69 native contracts in 184.36 seconds**, **98 extension and 17 browser
tests**, native/browser integration and 18-asset VSIX verification, with zero
failures/skips and all 16 job steps successful. Live direct-MCP graph and
rendered IDE acceptance remain pending; provider/parity counts are unchanged.
The initial HTTP start-guard failure remains separate historical evidence.
[Local69 evidence](evidence/native-graph-mcp-local-provenance.json),
[exact hosted69 evidence](evidence/native-graph-mcp-hosted-provenance.json),
[Direct MCP graph scope](native-graph-mcp.md).

The [native delegation design](native-delegation-design.md) now has a bounded
implementation and exact local/hosted71 acceptance above. It does not establish
runtime replanning, live multi-agent acceptance or verified OpenCode parity.

The ordered-Claude-history local checkpoint passed **67 native contracts in 117.65 seconds**,
with an exact manifest, zero failures/skips and no post-build exclusions. Strict
ordered Claude receipts preserve accepted raw tool inputs, escaped keys and
signed/interleaved blocks through actual AgentRunner file execution and xlang3
SQLite replay. The helper contract took **0.50 seconds** and expanded agent
contract **0.57 seconds**; unsigned-block rejection, receipt/DTO mismatch and
aggregate bounds also passed. Signatures/redacted data remain opaque, without
local cryptographic verification.

Fresh browser/native integration passed against the rebuilt server. Unchanged
frontend sources match `ae7c4ba` and retain its recorded **98 extension and 17
browser tests**, without rerunning those suites for that local native change.
Exact source `fceb50ba0493f645ee5f0a00d5400e0f103df231` passed its hosted
**67 native contracts in 120.18 seconds**, **98 extension and 17 browser tests**,
native/browser integration and VSIX verification, with zero failures/skips and
all 16 job steps successful. Its history helper took **0.31 seconds** and
actual Claude agent contract **1.03 seconds**. That hosted67 result excludes
newer direct-MCP-graph source and its separate local 69-contract gate.
[Exact hosted67 evidence](evidence/native-anthropic-history-hosted-provenance.json).
Thinking request controls, live models, rendered
IDE acceptance, account/model binding and foreign signed-history conversion
remain incomplete. [Local67 evidence](evidence/native-anthropic-history-local-provenance.json),
[Claude history scope](native-claude-history.md).

The preceding local Claude checkpoint passed **66 native contracts in 114.49 seconds**,
**98 extension and 17 browser tests**, with zero failures/skips, an exact native
manifest and no post-build exclusions. Actual Claude AgentRunner file reads,
matching tool results, xlang3 SQLite history/reopen, cancellation/recovery and
second-tool-row SQL rollback passed with synthetic provider replies. Native
usage events and persisted responses retain supplied uncached input/output/cache
counts and zeros without sums or invented totals; actual DOM fixtures verify
their live/history labels. Fresh browser/native integration passed against the
rebuilt server. The new agent contract took **0.56 seconds** and adapter contract
**0.23 seconds**. Exact source
`ae7c4ba07d59f57cbe393eff5a01b7d5186dfbc5` also passed its hosted **66 native
contracts in 131.92 seconds**, **98 extension and 17 browser tests**,
native/browser integration and VSIX verification, with zero failures/skips. Its
Claude agent contract took **1.41 seconds**.
[Exact hosted66 evidence](evidence/native-anthropic-agent-hosted-provenance.json).
[Local66 evidence](evidence/native-anthropic-agent-local-provenance.json),
[Claude scope](native-claude-agent.md).

That66 result excludes the newer ordered thinking/signature receipt source above.
It does not establish live inference, rendered Claude IDE acceptance or
acceptance of the 21 pinned Anthropic models. Provider
and OpenCode parity counts are unchanged; installed previews are unchanged.

The cleanup source `dea588874e5a8c8e40bf1a158fa925520443c8a0` passed its hosted
gate: **58 native, 88 extension and 17 browser contracts**, native/browser
integration and VSIX verification, with zero failures or skips.
[Exact cleanup evidence](evidence/native-cleanup-hosted-provenance.json).

The later source `ddd1d3da087f9ca7f8b0b8d81705c82632e15094` passed its separate
hosted gate: **60 native, 88 extension and 17 browser contracts**, native/browser
integration and VSIX verification, with zero failures or skips. This adds native
Gemini request/SSE/HTTP transport and signed replay data, using labelled
synthetic socket fixtures. It excludes the later common gateway/history bridge.
[Exact transport evidence](evidence/native-gemini-transport-hosted-provenance.json).

The gateway/history source `75f45f0a036f1ffbab8c4b157df364f3697b52a0` passed
its exact hosted gate: **61 native, 91 extension and 17 browser contracts**,
native/browser integration and VSIX verification, with zero failures/skips.
Its native gate took **121.84 seconds**. This includes common gateway/history
receipts, separate tool identities, supplied usage, strengthened callback
assertions and escaped large-response regressions. The earlier local 61 result's
post-build exclusions remain historical; this hosted gate executes those cases.
[Exact hosted evidence](evidence/native-gemini-history-hosted-provenance.json).

The subsequent agent source `c9591fe79cad9a4253ac8088933f0c8a2848ded1` passed
**62 native contracts locally in 100.44 seconds**, including actual native file
execution and signed replay after xlang3 SQLite reopen with synthetic provider
replies. Native/browser integration passed again against that compiled server;
its unchanged thin-client sources matched the earlier 91/17 pass.
[Local agent evidence](evidence/native-gemini-agent-local-provenance.json).
Its exact hosted gate also passed **62 native, 91 extension and 17 browser
contracts**, native/browser integration and VSIX verification, with zero
failures/skips. Its native gate took **128.80 seconds**.
[Exact hosted agent evidence](evidence/native-gemini-agent-hosted-provenance.json).
That hosted result excludes the new catalogue/enrollment source below.

Enrollment source `2f5e0f05e9bfadcf86b5508863da0ec5a9e78cfe` passed its
exact hosted **64 native, 94 extension and 17 browser contracts**, native/browser
integration and VSIX verification, with zero failures/skips. Its native gate
took **158.09 seconds**; the earlier local 64-contract gate took **120.98
seconds**. Native Gemini catalogue pagination, encrypted enrollment, separate
per-model tool policy, actual owned-key file execution/cancellation/CAS failure
and signed xlang3 SQLite replay passed with synthetic provider replies. The
direct-SQLite removal and shared `records.hpp` cleanup are included.
[Exact hosted enrollment scope](evidence/native-gemini-enrollment-hosted-provenance.json).

The preceding CLI source passed all **65 native contracts locally in 113.16 seconds**,
with its exact manifest, zero failures/skips and no post-build exclusions. The
new CLI contract took **4.23 seconds** and covers OpenAI/Claude/Gemini profile
catalogues, private environment setup, selection, all-family secret reflection
rejection, safe provider diagnostics, signed Gemini text/history/SQLite reopen,
stale ownership without retry and actual failed-turn/recovery status. Fresh
browser/native integration passed again; that checkpoint's unchanged frontend
sources retained the earlier verified **94 extension and 17 browser tests**,
without rerunning those suites for the CLI change.
[Local CLI scope](evidence/native-provider-profile-cli-local-provenance.json).
Exact CLI source `ac69c1f2a2c4a757b23d0d9da8b98d6188fab3a8` subsequently
passed its hosted **65 native contracts in 149.37 seconds**, **94 extension and
17 browser tests**, native/browser integration and VSIX verification, with zero
failures/skips. The CLI contract took **4.16 seconds**; the exact ac69 manifest,
pinned runtime/stdlib/patch and original logs were verified.
[Exact hosted CLI evidence](evidence/native-provider-profile-cli-hosted-provenance.json).
This hosted65 result excludes newer Claude66 agent/metrics and Claude67 history
source. GenerateContent
eligibility alone does not establish tool capability or live acceptance. Live
Gemini inference and actual Gemini IDE acceptance remain unverified. See
[provider setup](provider-setup.md), [current validation](VALIDATION_STATUS.md)
and [milestones](milestones.md).

An earlier installed browser preview used native `19d69dd` and view `6f32d215`;
its [historical scope](provider-setup.md) is preserved. The independently
accepted c4 installed preview has its own
[historical evidence](preview-checkpoint-c4ec09fc.md). The preceding exact e353
backend upgrade, separately repaired rendered browser and failed first live
delegation request are recorded in [current acceptance](native-delegation-acceptance.md).
They do not establish successful live delegation or installed VS Code acceptance.

Test counts describe verification scope, not a parity percentage. No OpenCode
operation is declared equivalent merely because a component contract passes.
Complete coding, provider, protocol, CLI and editor acceptance remains required.

## Pinned API inventory

The pinned source lives in the ignored `.agentflow/reference/opencode` checkout.
`Tools/audit-opencode.mjs` verifies its commit and reads the committed OpenAPI
schema, so local reference edits cannot silently change the baseline. It
generates [OPENCODE_API_INVENTORY.json](OPENCODE_API_INVENTORY.json): **136
operations**, with upstream parameters/body/responses and acceptance requirements.
The historical upstream audit lists 139; the pinned schema count is authoritative.

Removed-prototype mappings are absent. The native mapping identifies **23
partial C++ source candidates and 113 unmapped operations**. Each candidate
records a checked source anchor, SHA-256, surface and explicit gap. Unmapped
means no reviewed mapping, not proof that no related component exists. Source
presence and hashes do not establish behavioral equivalence or executed
CLI/editor acceptance. **Zero operations are claimed as verified parity.** The
API inventory does not cover every UI, plugin, model/provider or tool behavior.

## Native capability coverage and remaining acceptance

This table records current native components and delivery gaps. It replaces the
removed Python prototype's status table; it is not a completion score.

| Capability | Native component scope | Remaining acceptance |
| --- | --- | --- |
| Persistent sessions and run events | C++ repositories, ownership, transactions, replay and recovery use SQLite through embedded xlang3 | Complete concurrent-client conversations, failure/recovery and release acceptance |
| Agent/model/tool loop | Shared C++ single-agent/graph execution, hosted71 read-only delegation, hosted77 bounded dependency planning/revision and local78 coding-child MCP effects with durable budgets and signed continuation | Broad live coding tasks, live/rendered/installed planning acceptance, recursive/deterministic dynamic nodes, context bounds and production recovery |
| Models and providers | Native Chat Completions and Responses; Gemini catalogue/enrollment/owned-key execution is hosted-verified at 64, profile CLI controls at hosted65, Claude tools/metrics at hosted66 and ordered opaque signed receipts/actual SQLite replay at hosted67. New DeepSeek gateway/profile contracts passed with synthetic replies, and the third complete local 88 gate passed | Fix the subsequently reproduced Responses legacy text serialization and pass the fourth full gate; native live four-provider coding and installed-client acceptance, thinking controls, model/account binding and broad provider/authentication/capability coverage remain; direct account smoke checks are separate; see [MODEL_SUPPORT.md](MODEL_SUPPORT.md) |
| File/search/edit/process tools | Native workspace reads/search, approval-backed edits/creation and foreground process effects have scoped contracts | General patch review, background/PTY execution and complete repository coding tasks |
| Instructions and agent presets | Native repository guidance discovery, source binding, durable approval checks and explicit workspace.inspect revision1 leaf policy | Full instruction scoping/configuration and broader reproducible presets |
| Skills and commands | Native ownership is specified | Discovery, execution, lifecycle and CLI/editor acceptance |
| Plugins | Native ownership is specified | Lifecycle, hooks, isolation and compatibility acceptance |
| MCP | Native configured stdio tools, schema worker, approvals and modern/legacy official SDK peers have scoped contracts; hosted69 direct graph effects and local78 dynamically planned coding-child effects have separate acceptance scopes | Streamable HTTP/OAuth, resources/prompts, broader SDK features and full live coding interoperability |
| Permissions and policies | Backend-owned durable grant/deny/cancel and effect ownership have actual file/process/peer fixtures | Complete policy rules, recovery and consistent enforcement across exposed tools/protocols |
| Context compaction | Current native source implements Responses checkpoints/counting, session/mid-run and manual/automatic paths, private provider state and actual budgets within the third local 88/88 gate's scope; [design and status](native-context-compaction-design.md) | Preserve the full gate after the later serializer repair; complete non-Responses provider strategies, upgrade/rollback, installed thin-client and live coding acceptance; local contracts do not establish full compaction completion |
| Snapshots and recovery | Durable uncertainty inspection and non-replay safeguards cover selected effects | General reviewable snapshots, restoration and attributed reconciliation |
| Attachments and references | Native CLI/editor context work is documented in [CLI scope](native-interactive-cli.md) | Complete model-input, editor-context and multimodal acceptance |
| Formatters and diagnostics | Required by the native coding goal | Real project integrations and end-to-end verification |
| Search and network configuration | Native HTTP/TLS and workspace search components exist | Configured network/search behavior across coding workflows |
| Session sharing | Local backend sessions are shared across authorized views | Explicit export/sharing behavior and access-controlled output; team sharing belongs to Nexus |
| CLI/TUI | Native client supports authenticated conversation/run/approval/inspection flows plus profile catalogue/setup/selection, revision-bound admission and signed native history in the exact hosted65 scope | Complete interactive coding/terminal UX acceptance and execution of newer source at its own scope |
| Browser and VS Code | Thin browser and right-sidebar clients display native history, models, approvals and actual metrics within recorded scopes | Complete populated coding, diff/recovery/context and editor acceptance |
| A2A and agent graphs | Native shared-executor task controls and durable graphs have scoped contracts; hosted69 direct MCP effects, hosted71 read-only delegation, hosted77 bounded planning/revision and local78 coding-child MCP effects are distinct results. The installed ace/schema-v10 preview completed a real OpenAI two-child read/join; its repaired browser view was rendered | Installed VS Code delegation and live/rendered/installed planning acceptance, recursive/deterministic dynamic nodes, outbound remote delegation and remaining protocol interoperability |
| Local/Nexus profiles | Shared protocol and ownership boundaries are specified | Profile isolation, authenticated enrollment and reconnect/lease/recovery acceptance; Nexus implements private coordination |

Themes, warming and additional build/API surfaces need comparison against the
pinned source. Expand the inventory as concrete acceptance requirements are
reviewed.

## Historical native checkpoints

Source `46262d6ef7949a9caf778ccb6cf74733ef28b5ac` passed **52 native, 73
extension and 14 browser contracts** in its
[hosted gate](https://github.com/xlang-foundation/xMind/actions/runs/37709335756).
[Exact evidence](evidence/native-model-protocol-diagnostics-hosted-provenance.json).
Its recorded browser rename, cancel, refresh, reopen and CLI navigation acceptance
is [historical live evidence](evidence/live-browser-cli-navigation-rename.json),
not the current installed-bundle claim. Other actual Responses/tool successes
and the original failed MCP read remain in
[live-provider compatibility](live-provider-compatibility.md).

Graph source `bd059a46781292640d95943e00b0d86a4891936c` passed **44 native and
49 extension contracts** in its
[hosted gate](https://github.com/xlang-foundation/xMind/actions/runs/37682783904).
The actual human/tool graph and restored IDE transcript have their own
[recorded scope](vscode-graph-workflows.md). The earlier foreground-process source
`3297a4bce2d591c0f1c1dd37ccc18a15297369e7` passed **35 native and 40 extension
contracts**; [milestones](milestones.md) preserve its exact evidence. These
versioned results prove bounded components and do not establish full parity.
