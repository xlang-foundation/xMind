# OpenCode 2 feature baseline

The user now requires native C++ implementation. OpenCode is a reference only. Existing Python source equivalents are historical prototypes and do not count as delivered native capabilities; the status table below records prototype evidence. See `NATIVE_ARCHITECTURE.md`.

Baseline: `anomalyco/opencode` tag `v2.0.16`, commit `3a103fe0aff726a4edc7492f03f7b88195d9e4c9`, verified with `git ls-remote`. Source: https://github.com/anomalyco/opencode/tree/v2.0.16 . Documentation discovery: https://opencode.ai/v2/docs . Current documentation may differ from this pinned release; source inspection is required before finalizing each acceptance criterion.

This is an initial inventory, not a claim of parity. All unverified features remain incomplete.

## Current native progress

The following historical table records prototype coverage, not native implementation status. Current C++ checkpoints and their exact verification scopes are in [milestones](../doc/milestones.md): eighteen native contracts pass through local approval API/file effects; nineteen extension contracts pass through read-only diff comparison and reconnect/model preferences. Model-invoked edits and new native model/response metadata remain pending compilation. No OpenCode operation or capability is considered fully equivalent merely because a component contract passes. Full API/tool/plugin/context/CLI/editor/provider acceptance remains required against the pinned inventory.

The pinned source is cloned locally under `.agentflow/reference/opencode`. `Tools/audit-opencode.mjs` verifies its commit and maps the actual OpenAPI schema into `Documents/OPENCODE_API_INVENTORY.json`: 136 operations. The historical upstream audit document lists 139; the pinned schema count is authoritative. Every operation includes upstream parameters/body/responses and acceptance requirements. Twelve have partial prototype source equivalents; 124 have no mapped equivalent. None is verified parity. API inventory alone does not cover all UI, plugin, model/provider or tool behavior.

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
