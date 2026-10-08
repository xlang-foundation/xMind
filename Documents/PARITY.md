# OpenCode 2 feature baseline

The user now requires native C++ implementation. OpenCode is a reference only. Existing Python source equivalents are historical prototypes and do not count as delivered native capabilities; the status table below records prototype evidence. See `NATIVE_ARCHITECTURE.md`.

Baseline: `anomalyco/opencode` tag `v2.0.16`, commit `3a103fe0aff726a4edc7492f03f7b88195d9e4c9`, verified with `git ls-remote`. Source: https://github.com/anomalyco/opencode/tree/v2.0.16 . Documentation discovery: https://opencode.ai/v2/docs . Current documentation may differ from this pinned release; source inspection is required before finalizing each acceptance criterion.

This is an initial inventory, not a claim of parity. All unverified features remain incomplete.

## Current native progress

The latest inspected successful native checkpoint is
`46262d6ef7949a9caf778ccb6cf74733ef28b5ac`: 52 native, 73 extension and 14 browser
contracts passed its [hosted gate](https://github.com/xlang-foundation/xMind/actions/runs/37709335756).
[Exact evidence](../doc/evidence/native-model-protocol-diagnostics-hosted-provenance.json).
This includes native CLI conversation navigation and optimistic title changes.
The exact native/browser pair is now installed in the preview: real browser
rename, Cancel, refresh, native reopen and read-only CLI navigation passed with
history/runs/provider settings preserved. [Live scope](../doc/evidence/live-browser-cli-navigation-rename.json).
Actual editor rename acceptance remains separate.
The later cookie-duration checkpoint `e9f562a` passed 52 native, 72 extension and
14 browser contracts; [exact scope](../doc/evidence/native-view-session-duration-hosted-provenance.json).
The matched duration/diagnostic/catalogue pair is installed as `46262d6`. Newer CLI
attachment/model-free inspection changes await their own gate.
The browser preview now uses tested checkpoint `46262d6`. Actual live Responses
text, read, approved edit and registered Git status turns succeeded within
[their recorded scope](../doc/live-provider-compatibility.md). An official MCP
peer's original model/tool read failed before dispatch;
[failed acceptance evidence](../doc/evidence/live-responses-mcp-read.json).
A separate approved stdio read and live continuation completed on the newer
verified pair; [bounded success evidence](../doc/evidence/live-responses-mcp-diagnostic-read.json).
The original failure's cause remains unproven, and full MCP parity is incomplete.
Dynamic multi-agent delegation and complete live coding parity remain incomplete.
The older checkpoint summaries and prototype table below remain historical
evidence, not the current product status or a claim of full parity.

The current native graph backend checkpoint `bd059a46781292640d95943e00b0d86a4891936c` passed **44 native and 49 extension contracts** in [hosted CI](https://github.com/xlang-foundation/xMind/actions/runs/37682783904). Later sidebar source passed **60 extension contracts locally**, and a real human/tool graph completed and restored its transcript in the actual IDE. [Current graph/editor evidence](../doc/vscode-graph-workflows.md). Native A2A task controls are a new component under verification; message admission, streaming, SDK interoperability, task-local history and remote delegation remain required. These are component milestones, not verified OpenCode parity. The older counts/table below retain their historical prototype/source scope.

The following historical table records prototype coverage, not native implementation status. Current C++ checkpoints and exact verification scopes are in [milestones](../doc/milestones.md): **35 native and 40 extension contracts passed isolated CI**, source `3297a4bce2d591c0f1c1dd37ccc18a15297369e7`, including approved edits/creation, stored response metadata, registered MCP tools with official SDK peers, executable-bound foreground commands, descendant cleanup, durable retained output and native watcher reconnect. Real subprocess/file/database effects are verified with synthetic inference; live coding completion, full shell/background/PTY and broader SDK features remain pending. No OpenCode operation or capability is considered fully equivalent merely because a component contract passes. Full API/tool/plugin/context/CLI/editor/provider acceptance remains required against the pinned inventory.

The pinned source is cloned locally under `.agentflow/reference/opencode`. `Tools/audit-opencode.mjs` verifies its commit and reads the committed OpenAPI schema object, so local reference edits cannot silently change the baseline. It generates `Documents/OPENCODE_API_INVENTORY.json`: 136 operations. The historical upstream audit document lists 139; the pinned schema count is authoritative. Every operation includes upstream parameters/body/responses and acceptance requirements. Twelve have historical prototype mappings. The separate native mapping identifies **23 partial C++ source candidates and 113 unmapped operations**. Each candidate records a checked source anchor, source SHA-256, candidate surface and explicit gap. Unmapped means no reviewed mapping, not proof that no related component exists. Neither source presence nor these hashes establish behavioral equivalence, executed contracts or CLI/editor acceptance. **Zero operations are claimed as verified parity.** API inventory alone does not cover all UI, plugin, model/provider or tool behavior.

| Capability | AgentFlow status | Required evidence |
| --- | --- | --- |
| Persistent sessions and run events | Initial xlang3 tests passed; current revalidation has intermittent failures under investigation | Resolve failures; full conversations and reconnect across clients |
| Agent/model/tool loop | Initial streaming test-double checks passed; intermittent revalidation failures remain | Resolve failures; live providers, retry policy, context bounds and production recovery |
| Models and providers | OpenAI-compatible streaming source implemented; LiteLLM provider/model reference pinned and audited | xlang3 SDK dependency probes, provider configuration, model selection, capability validation, frontier-model interoperability, authentication and error handling; see MODEL_SUPPORT.md |
| File/search/edit/shell tools | Workspace read/list/write/search/exact replacement implemented; search/edit permissions tested | Shell, patch review and end-to-end repository validation |
| Instructions and agent presets | Pending | Repository instruction scoping and reproducible configuration |
| Skills and commands | Pending | Discovery and execution from CLI/editor |
| Plugins | Pending | Lifecycle, extension hooks and isolation |
| MCP servers | Stateless HTTP server verified with official Node SDK; HTTP client and tool allowlists implemented, test-double checks pass | Outbound live TCP cancellation unresolved; stdio, authentication and broader interoperability pending |
| Permissions and policies | Per-operation approval source/API/CLI/editor controls implemented; test blocked at SQLite transition | Full grant/deny/cancel verification, policy rules and consistent remote enforcement |
| Context compaction | Pending | Bounded context with preserved continuation state |
| Snapshots and recovery | Pending | Reviewable change history and verified restoration |
| Attachments and references | Pending | Model input and editor context integration |
| Formatters and diagnostics | Pending | Real project diagnostic and formatter integrations |
| Search and network configuration | Pending | Configured network behavior and search integration |
| Session sharing | Pending | Explicit user action, access control and usable shared output |
| CLI/TUI | Backend and session/run/events/cancel commands; actual xlang3 HTTP session smoke passed | Interactive coding, approval UI and full session lifecycle |
| Editor client | VSIX packaged; shared backend client/reconnect tests pass | Actual VS Code UI/install validation, diff review and approval flows |
| Web/desktop | Later phase | Functional clients over shared API |
| A2A and agent graphs | Graph source/API implemented but closure failure blocks validation; A2A JSON-RPC adapters implemented but end-to-end task failed | Runtime/persistence resolution, independent peers, durable graphs and remote delegation |

Themes, warming, and further API/build surfaces require the pinned-source audit. Expand this table as the audit identifies additional features.
