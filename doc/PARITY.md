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

The committed history/gateway source `75f45f0a036f1ffbab8c4b157df364f3697b52a0`
awaits its own hosted result. Its compiled local baseline passed **61 native,
91 extension and 17 browser contracts**, plus native/browser integration.
Stronger callback assertions and the large function-response bound correction
were added after compilation and are explicitly excluded from that pass claim.
[Local baseline and exclusions](evidence/native-gemini-history-local-provenance.json).
The newer Gemini AgentRunner contract increased the native manifest to **62**;
all 62 passed locally in **100.44 seconds**, including the callback/large-result
cases excluded above. Actual native file execution and signed replay after
xlang3 SQLite reopen passed with synthetic provider replies. Current thin-client
sources retain their 91/17 pass, and native/browser integration passed again
against the rebuilt server. [Current local evidence](evidence/native-gemini-agent-local-provenance.json).
Live Gemini inference and product discovery/enrollment remain incomplete.
See [current validation](VALIDATION_STATUS.md) and
[milestones](milestones.md).

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
| Models and providers | Native Chat Completions, Responses and Anthropic Messages components; Gemini transport is hosted-verified, newer gateway/history source is pending as scoped above | Gemini agent/enrollment/discovery acceptance and broad provider/authentication/capability coverage; see [MODEL_SUPPORT.md](MODEL_SUPPORT.md) |
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
| CLI/TUI | Native client supports authenticated conversation/run/approval/inspection flows | Complete interactive coding and terminal UX acceptance |
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
