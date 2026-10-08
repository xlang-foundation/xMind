# Dynamic agents and registered workflows

Normal coding uses **Agent** mode. The user provides an objective and the model selects the next registered tool from the current conversation and observed results. The native engine executes or requests approval, records the actual result, and invokes the model again. A failed test can lead to another edit and verification attempt. The user does not need to select a predefined file-reading graph to code.

The shared UI calls the selector **Run mode**, with **Agent (default)** as its first option and the initial selection. Agent retains the existing empty workflow identifier and single-run admission path. Registered graph options follow it and select an immutable backend graph and revision. This label change does not implement new execution behavior or override an explicitly saved workflow choice.

An earlier selector-only webpage check reconnected with its selected model/history preserved and confirmed Agent was first and selected ahead of `read.repository.file`; its 63 extension contracts are historical evidence for that view change. The later native delegation checkpoint has its own source-bound evidence below.

## Existing implementation

`Native/src/agent_runner.cpp` owns the model/tool loop, cancellation/deadline checks, typed tool invocation, exact effect proposals and durable conversation continuation. A provider response can finish or request registered tools; tool results become the next model input. The configured turn limit remains a real bound, rather than an unlimited execution claim. Repository instructions, model selection, credentials and policy are backend-owned.

Agent nodes inside a registered graph invoke this same dynamic engine. The graph determines dependencies between nodes; the model still chooses the tool steps inside an agent node. Human/tool nodes provide explicit decisions or deterministic tool execution. Registered workflows remain useful for repeatable processes, review gates and parallel independent work.

Checkpoint `e353a37799530a234a6fa13e51f61a5c52d3ae6a` adds native `delegate_tasks` to eligible ordinary Agent roots. The parent model selects independent `workspace.inspect` revision 1 investigations; actual child AgentRunners make provider/tool calls with isolated histories, return observed outcomes, and share the root's deadline, cancellation, child/concurrency limits and durable model-call budget. Leaves are explicitly read-only; the general parent keeps its configured coding tools and exact approvals. A failed child can lead to a new parent-selected batch without replaying the old one.

The [local delegation evidence](evidence/native-delegation-local-provenance.json) records a complete **71/71 native gate in 142.36 seconds**, **103 extension contracts** and **19 browser contracts**, plus browser/native integration and 18 verified VSIX assets. The same exact e353 checkpoint passed its [hosted gate](https://github.com/xlang-foundation/xMind/actions/runs/37765666399): **71 native in 175.51 seconds**, **103 extension in 2.9668103 seconds** and **19 browser in 1.426115 seconds**, plus native/browser integration and all 18 VSIX assets, with zero failures/skips. [Exact hosted source/artifact/pin evidence](evidence/native-delegation-hosted-provenance.json). The delegation contracts use labelled synthetic provider replies with real native engines, filesystem effects, approvals, xlang3 SQLite, HTTP/CLI and thin controllers. [native-delegation.md](native-delegation.md) describes the implemented boundary.

The exact hosted backend is now installed with schema v10. Its original webpage failed classic-script startup; a separate local repair passed **103 extension contracts in 1.6557025 seconds** and **20 browser contracts in 0.9090882 seconds**, native/browser integration and all 18 VSIX assets. The repaired rendered page reused its cookie, prior history/metrics, model selection and Agent mode. The first subsequent live `delegate_tasks` request failed with `responses_terminal_mismatch` before admitting any children. Successful live delegation/join and installed VS Code acceptance remain unverified. [Separate upgrade, frontend and live-failure scopes](native-delegation-acceptance.md).

This is consistent with [Cursor's documented coding-agent loop](https://prod.cursor.com/help/ai-features/coding-agents): each tool result informs the next action. Cursor is a behavior reference, not an implementation dependency. The pinned OpenCode inventory and full parity acceptance requirements remain unchanged.

## Remaining dynamic planning target

Bounded independent delegation is implemented. An agent must also be able to select dependencies, human checks and authorized coding subtasks, then revise undispatched work using actual observations. This still requires native implementation rather than client-side graph generation:

1. Registered planning tools propose typed tasks with objectives, dependencies and backend-offered execution presets. Provider, workspace, resource bounds and capability bindings come from the owning backend; model output cannot choose arbitrary execution destinations.
2. The C++ planner validates identifiers, acyclic dependencies, task/concurrency limits and capability/policy constraints. Model output cannot expand authority or create credentials/endpoints.
3. A revision-checked repository transaction records the accepted plan revision and its event together. Completed nodes, recorded effects and already claimed work remain immutable; revisions cannot silently replay or replace them.
4. The native scheduler owns dependency-aware child admission and independent conversations. The existing bounded leaf scheduler is a foundation, not the complete planner. Joins use observed results with activity identities and per-child metrics. Coding presets and deterministic effect nodes must use the existing exact controller approvals and journals. Human answers cannot grant effect authority, and uncertain effects cannot become successful dependency outputs.
5. Replanning can add or change undispatched tasks, skip unnecessary work, or request human input. It must preserve cancellation, deadlines and the parent run's remaining resource budget. A final human answer must wake the same owner to settle its pending plan result and use the held parent continuation even when no child remains to dispatch.
6. Remote A2A delegation records peer/task identity, scoped credentials and observed status. Timeout or disconnection cannot be fabricated as remote completion. Outbound delegation remains incomplete.
7. CLI, VS Code and the webpage render the same committed plan revisions, human requests and child events. They do not own planning or scheduling. Nexus clients can consume that protocol separately; private team-server/Electron features remain outside the OSS implementation scope.

Native skills and context compaction also remain required. Skills must preserve backend instruction/tool authority, and compaction must preserve same-provider continuation fidelity and observed effect/history ownership. Neither is supplied by the new delegation tool.

The [next native planning design](native-dynamic-plan-design.md) maps these requirements to revisioned transactions, a distinct dynamic-plan ledger, shared budgets, generic child observations and reusable graph topology contracts. It is an implementation proposal, not completion evidence.

Acceptance must prove actual model-selected dependency subtasks and human checks, dependency changes after observed failures, independent child histories, real authorized coding effects, cancellation and crash/reconnect without replay. The passing independent-leaf contracts and existing fixed-graph tests cover their stated scopes; they do not prove this broader planning capability.

Current next work retains the complete coding/provider/protocol goal. Dependency planning, revisioned replanning, broader authorized child execution, skills, compaction, remote delegation and full live coding acceptance remain required. The implemented independent leaves and registered `read.repository.file` utility workflow do not replace them.
