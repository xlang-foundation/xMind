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

The current local checkpoint passed **67 native contracts in 117.65 seconds**,
with an exact manifest, zero failures/skips and no post-build exclusions. Strict
ordered Claude receipts preserve accepted raw tool inputs, escaped keys and
signed/interleaved blocks through actual AgentRunner file execution and xlang3
SQLite replay. The helper contract took **0.50 seconds** and expanded agent
contract **0.57 seconds**; unsigned-block rejection, receipt/DTO mismatch and
aggregate bounds also passed. Signatures/redacted data remain opaque, without
local cryptographic verification.

Fresh browser/native integration passed against the rebuilt server. Unchanged
frontend sources match `ae7c4ba` and retain its recorded **98 extension and 17
browser tests**, without rerunning those suites for this native change. Current
hosted67 validation is pending. Thinking request controls, live models, rendered
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

The installed browser preview is a separate native `19d69dd` and view `6f32d215`
pair. Current source or CI success does not establish a preview upgrade.
[Installed scope](provider-setup.md).

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
| Agent/model/tool loop | Shared C++ single-agent and graph execution with durable admission, cancellation and tool continuation | Broad live coding tasks, dynamic delegation, context bounds and production recovery |
| Models and providers | Native Chat Completions and Responses; Gemini catalogue/enrollment/owned-key execution is hosted-verified at 64, profile CLI controls at hosted65, Claude tools/metrics at hosted66 and ordered opaque signed receipts/actual SQLite replay at local67 | Exact hosted verification of newer Claude source, thinking request controls and live Claude/Gemini acceptance, model/account binding, pinned-model and broad provider/authentication/capability coverage; see [MODEL_SUPPORT.md](MODEL_SUPPORT.md) |
| File/search/edit/process tools | Native workspace reads/search, approval-backed edits/creation and foreground process effects have scoped contracts | General patch review, background/PTY execution and complete repository coding tasks |
| Instructions and agent presets | Native repository guidance discovery, source binding and durable approval checks | Full instruction scoping/configuration and reproducible presets |
| Skills and commands | Native ownership is specified | Discovery, execution, lifecycle and CLI/editor acceptance |
| Plugins | Native ownership is specified | Lifecycle, hooks, isolation and compatibility acceptance |
| MCP | Native configured stdio tools, schema worker, approvals and modern/legacy official SDK peers have scoped contracts | Streamable HTTP/OAuth, resources/prompts, broader SDK features and full live coding interoperability |
| Permissions and policies | Backend-owned durable grant/deny/cancel and effect ownership have actual file/process/peer fixtures | Complete policy rules, recovery and consistent enforcement across exposed tools/protocols |
| Context compaction | Required by the native coding goal | Bounded context with preserved instructions, provider state and continuation |
| Snapshots and recovery | Durable uncertainty inspection and non-replay safeguards cover selected effects | General reviewable snapshots, restoration and attributed reconciliation |
| Attachments and references | Native CLI/editor context work is documented in [CLI scope](native-interactive-cli.md) | Complete model-input, editor-context and multimodal acceptance |
| Formatters and diagnostics | Required by the native coding goal | Real project integrations and end-to-end verification |
| Search and network configuration | Native HTTP/TLS and workspace search components exist | Configured network/search behavior across coding workflows |
| Session sharing | Local backend sessions are shared across authorized views | Explicit export/sharing behavior and access-controlled output; team sharing belongs to Nexus |
| CLI/TUI | Native client supports authenticated conversation/run/approval/inspection flows plus profile catalogue/setup/selection, revision-bound admission and signed native history in the exact hosted65 scope | Complete interactive coding/terminal UX acceptance and execution of newer source at its own scope |
| Browser and VS Code | Thin browser and right-sidebar clients display native history, models, approvals and actual metrics within recorded scopes | Complete populated coding, diff/recovery/context and editor acceptance |
| A2A and agent graphs | Native shared-executor task controls, admission, discovery/stream/history and durable graph components have scoped contracts | Remote delegation, remaining protocol interoperability and live multi-agent acceptance |
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
