# Dynamic agents and registered workflows

Normal coding uses **Agent** mode. The user provides an objective and the model selects the next registered tool from the current conversation and observed results. The native engine executes or requests approval, records the actual result, and invokes the model again. A failed test can lead to another edit and verification attempt. The user does not need to select a predefined file-reading graph to code.

The shared UI calls the selector **Run mode**, with **Agent (default)** as its first option and the initial selection. Agent retains the existing empty workflow identifier and single-run admission path. Registered graph options follow it and select an immutable backend graph and revision. This label change does not implement new execution behavior or override an explicitly saved workflow choice.

The actual webpage was reconnected with its selected model/history preserved, and DOM inspection confirmed Agent was first and selected ahead of `read.repository.file`. All 63 extension contracts passed. This verifies the view choice/order, not dynamic multi-agent planning.

## Existing implementation

`Native/src/agent_runner.cpp` owns the model/tool loop, cancellation/deadline checks, typed tool invocation, exact effect proposals and durable conversation continuation. A provider response can finish or request registered tools; tool results become the next model input. The configured turn limit remains a real bound, rather than an unlimited execution claim. Repository instructions, model selection, credentials and policy are backend-owned.

Agent nodes inside a registered graph invoke this same dynamic engine. The graph determines dependencies between nodes; the model still chooses the tool steps inside an agent node. Human/tool nodes provide explicit decisions or deterministic tool execution. Registered workflows remain useful for repeatable processes, review gates and parallel independent work.

This is consistent with [Cursor's documented coding-agent loop](https://prod.cursor.com/help/ai-features/coding-agents): each tool result informs the next action. Cursor is a behavior reference, not an implementation dependency. The pinned OpenCode inventory and full parity acceptance requirements remain unchanged.

## Dynamic multi-agent target — not implemented yet

An agent should also be able to plan, delegate and revise a collection of subtasks at runtime. This requires native implementation rather than client-side graph generation:

1. A registered planning/delegation tool proposes typed tasks with objectives, dependencies, allowed tools/workspaces, model requirements and resource bounds.
2. The C++ planner validates identifiers, acyclic dependencies, task/concurrency limits and capability/policy constraints. Model output cannot expand authority or create credentials/endpoints.
3. A revision-checked repository transaction records the accepted plan revision and its event together. Completed nodes, recorded effects and already claimed work remain immutable; revisions cannot silently replay or replace them.
4. The native scheduler owns child admission and independent conversations. Joins use observed results with activity identities and per-child metrics. Failed or uncertain effects require their existing recovery path before dependent work proceeds.
5. Replanning can add or change undispatched tasks, skip unnecessary work, or request human input. It must preserve cancellation, deadlines and the parent run's remaining resource budget.
6. Remote A2A delegation records peer/task identity, scoped credentials and observed status. Timeout or disconnection cannot be fabricated as remote completion. Outbound delegation remains incomplete.
7. CLI, VS Code, HTML and Electron render the same committed plan revisions and child events. They do not own planning or scheduling.

Acceptance must prove actual model-selected subtasks, dependency changes after observed failures, independent child histories, real tool effects and approvals, cancellation and crash/reconnect without replay. Existing fixed-graph tests and a successful chat response do not prove this dynamic multi-agent capability.

Current next work retains the complete coding/provider/protocol goal. Dynamic task admission, replanning, remote delegation and full live coding acceptance remain required; they are not replaced by the registered `read.repository.file` utility workflow.
