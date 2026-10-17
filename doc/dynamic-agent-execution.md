# Dynamic agents and registered workflows

The initial `3fc4204` planning source also passed its [exact hosted gate](https://github.com/xlang-foundation/xMind/actions/runs/37808089507): **77 native / 119 extension / 26 browser** tests, all 16 successful steps, model-free integration and 18 verified VSIX assets. Downloaded archives and original logs match their recorded digests. Installation, rendered/live planning and editor acceptance remain separate. [Hosted source and scope evidence](evidence/native-dynamic-plan-hosted-provenance.json).

Normal coding uses **Agent** mode. The user provides an objective and the model selects the next registered tool from the current conversation and observed results. The native engine executes or requests approval, records the actual result, and invokes the model again. A failed test can lead to another edit and verification attempt. The user does not need to select a predefined file-reading graph to code.

The shared UI calls the selector **Run mode**, with **Agent (default)** as its first option and the initial selection. Agent retains the existing empty workflow identifier and single-run admission path. Registered graph options follow it and select an immutable backend graph and revision. This label change does not implement new execution behavior or override an explicitly saved workflow choice.

An earlier selector-only webpage check reconnected with its selected model/history preserved and confirmed Agent was first and selected ahead of `read.repository.file`; its 63 extension contracts are historical evidence for that view change. The later native delegation checkpoint has its own source-bound evidence below.

## Existing implementation

`Native/src/agent_runner.cpp` owns the model/tool loop, cancellation/deadline checks, typed tool invocation, exact effect proposals and durable conversation continuation. A provider response can finish or request registered tools; tool results become the next model input. The configured turn limit remains a real bound, rather than an unlimited execution claim. Repository instructions, model selection, credentials and policy are backend-owned.

Agent nodes inside a registered graph invoke this same dynamic engine. The graph determines dependencies between nodes; the model still chooses the tool steps inside an agent node. Human/tool nodes provide explicit decisions or deterministic tool execution. Registered workflows remain useful for repeatable processes, review gates and parallel independent work.

Current native source also accepts model-authored dependency plans with `agent`,
`human`, and deterministic `tool` nodes. Tool nodes are checked against the
owner's sealed catalogue and admitted workspace. Supported routes include
workspace reads, approved synchronous `run_process`, registered MCP tools,
resources and prompts, and registered outbound A2A calls. MCP, process, A2A,
and coding effects retain their normal native approval and durable outcome
rules. Nested plans share the root budget and are bounded to depth eight; the
engine fixture exercises nested pauses, joins, restart and depth-two work.
Current-source tests pass **4/4** for dynamic plan parsing, repository,
execution and MCP/process/A2A nodes, and **10/10** for context/skill contracts.
Live OpenAI Responses planning now also passes in the rendered webpage and VS
Code sidebar: a read-only `workspace.inspect` child completed, a human node
paused both runs, and each run completed after the answer without file or
command effects. The webpage pause survived reload. The VS Code **Open Browser
View** command opened a browser attached to the same backend; a browser
follow-up appeared in the VS Code history after sidebar refresh/reconnect.
[Exact source, runtime hashes and scope](evidence/live-planning-shared-backend-20261017.json).
The native test provider and peer responses remain fixtures; the UI planning
checks use live OpenAI inference.

Checkpoint `e353a37799530a234a6fa13e51f61a5c52d3ae6a` adds native `delegate_tasks` to eligible ordinary Agent roots. The parent model selects independent `workspace.inspect` revision 1 investigations; actual child AgentRunners make provider/tool calls with isolated histories, return observed outcomes, and share the root's deadline, cancellation, child/concurrency limits and durable model-call budget. Leaves are explicitly read-only; the general parent keeps its configured coding tools and exact approvals. A failed child can lead to a new parent-selected batch without replaying the old one.

The [local delegation evidence](evidence/native-delegation-local-provenance.json) records a complete **71/71 native gate in 142.36 seconds**, **103 extension contracts** and **19 browser contracts**, plus browser/native integration and 18 verified VSIX assets. The same exact e353 checkpoint passed its [hosted gate](https://github.com/xlang-foundation/xMind/actions/runs/37765666399): **71 native in 175.51 seconds**, **103 extension in 2.9668103 seconds** and **19 browser in 1.426115 seconds**, plus native/browser integration and all 18 VSIX assets, with zero failures/skips. [Exact hosted source/artifact/pin evidence](evidence/native-delegation-hosted-provenance.json). The delegation contracts use labelled synthetic provider replies with real native engines, filesystem effects, approvals, xlang3 SQLite, HTTP/CLI and thin controllers. [native-delegation.md](native-delegation.md) describes the implemented boundary.

The historical e353 webpage failed classic-script startup; its separate local repair passed **103 extension contracts in 1.6557025 seconds** and **20 browser contracts in 0.9090882 seconds**, native/browser integration and all 18 VSIX assets. The first repaired e353 live delegation failed before child admission. The installed preview now uses the separately verified **ace2460/schema-v10** backend and retained repaired view: one real OpenAI parent/two-child read-only join and immediate refresh passed at their recorded scope. Installed VS Code acceptance remains unverified. [Separate upgrade, frontend and live scopes](native-delegation-acceptance.md), [reasoning checkpoint](native-responses-reasoning.md).

The initial native dependency-planning source checkpoint adds `plan_tasks`, `revise_plan` and `inspect_plan` to eligible ordinary roots. The model selects agent/human dependency tasks and may revise only never-claimed work. Real `workspace.inspect` and approval-controlled `workspace.coding` children share the same root budget and child pool; clean human pauses retain that owner and its once-only signed continuation. Schema v11 durably records capabilities, revisions, claims, human input, active-time segments and observed outcomes. Registered graphs keep their separate immutable identity and APIs. [Initial implementation](native-dynamic-plan.md).

The complete guarded local **77-contract gate passed in 153.88 seconds**, with the exact expected/registered/passed manifest and all **421 frozen source hashes** verified. The native engine contract performs actual parallel reads, observed failure, same-plan revision, human pause/reopen, separate edit approval, real coding and dependent verification using synthetic provider replies/signatures/keys. **119 extension and 26 browser contracts** passed at their renderer/controller scope. Model-free browser/native graph/human/auth/cookie/reconnect integration and verification of all **18 required VSIX assets** also passed, without rendered dynamic-planning or installed-editor acceptance. [Local source and gate evidence](evidence/native-dynamic-plan-local-provenance.json). Schema-v11 installation, live planning and installed VS Code acceptance remain pending. Actual dynamic coding-child MCP peer effects are not established by these gates.

This is consistent with [Cursor's documented coding-agent loop](https://prod.cursor.com/help/ai-features/coding-agents): each tool result informs the next action. Cursor and OpenCode are behavior references, not implementation dependencies or compatibility targets. xMind defines its own coding and editor acceptance requirements.

## Remaining product acceptance

The native plan engine and deterministic execution routes are implemented and
covered by current-source contracts. The remaining work is broader acceptance
and behavior:

1. Dependency conditions currently support `success` and `observed`. Add any
   further conditions only with explicit native semantics and durable outcome
   rules; model output cannot expand permissions, credentials or endpoints.
2. Extend live planning acceptance beyond the completed read-only agent plus
   human scenario: a current webpage run now completed a read-only
   `workspace.inspect` node followed by a dependent native `run_process` node;
   `npm.cmd run build` exited 0. The exact plan and process records are in the
   [live process-plan evidence](evidence/live-dynamic-plan-process-20261017.json).
   A plan that combines an approved code change with its registered
   verification command, and then refreshes both clients to verify shared
   results and supplied metrics, remains outstanding.
3. Rendered human questions, read-only child outcomes and shared conversation
   history are now verified in both development clients. Plan revisions,
   effect approvals and coding-child results still need current-backend live
   acceptance in the webpage and VS Code extension.
4. Continue A2A acceptance against independent peers: live model-selected
   dispatch and continuation, reconnect/recovery, streaming and cancellation
   acknowledgement, and the relevant official-SDK paths. A local peer fixture
   proves the native contract only.
5. Finish provider/context acceptance. Native skills have source contracts and
   separate live provider activation evidence; rendered installed skill
   controls still need acceptance. Responses context compaction has native
   implementation/contracts, while installed/live execution and compatible
   Claude, Gemini and Chat Completions strategies remain incomplete.

The planner, scheduler, event journal and client boundaries are described in
the [broader native planning design](native-dynamic-plan-design.md). Historical
implementation notes remain useful for their exact checkpoints; current
capabilities and evidence are recorded in the [current-source validation
record](evidence/agentflow-current-native-capabilities-20261017.json).
Nexus team-server, PostgreSQL, WebRTC and Electron implementation stays outside
xMind OSS.
