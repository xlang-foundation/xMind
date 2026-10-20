# xMind Runtime, Coding Harness and AI Model Gateway

Agreed product architecture, October 9, 2026. This specifies the final product;
the implementation status below distinguishes working components from pending
integration. The diagram is [architecture.svg](architecture.svg).

## Three core components

**Agent Runtime** owns AgentFlow: a single agent or an agent graph, optional
planning, graph readiness, scheduling, cancellation, budgets and recovery.
An agent is an LLM-driven execution instance. Its normal loop requests a model,
executes permitted tools and supplies actual results to the next model turn.
Another turn is not automatically another agent. Delegation explicitly creates
a child execution. The agent loop currently runs in the backend process. A
trusted registered xlang3 tool can execute in-process or in a short-lived
`xmind` worker; durable agent-level worker placement remains pending.

**Coding Agent / Harness** is a first-class application of that runtime. It
supplies coding agent definitions, repository instructions, skills, workspace
and editor context, native file/search/edit/patch/Git/process tools, diagnostics,
reviewable diffs and coding verification. It uses the general agent loop and
shared tool permissions. The VS Code host supplies editor/workspace context;
the harness executes in the backend. It can be used directly or as an agent
node inside a graph/super-agent.

**AI Model Gateway** owns native provider adapters, encrypted credential
resolution, account model discovery, per-model capabilities, request/response
translation, streaming, cancellation and real usage reporting. Agents and
planners use this gateway. Provider wire formats are kept behind its contracts.
Support for a provider does not imply support for every model or feature.

Old xMind informs general-agent, graph, callable and model-service behavior.
OpenCode informs coding harness behavior, tool loops, context and client UX.
LiteLLM informs provider/model coverage and adapter behavior. These are design
references; their implementations/SDKs are not the production platform.
The shared core and provider/protocol implementations remain native C++.
Embedded xlang3 runs scripts, skills and compatible pure-Python libraries and
performs all SQLite I/O. No CPython interpreter or native-extension fallback.

## Public concepts and authoring

| Concept | Contract |
| --- | --- |
| Agent | LLM-driven behavior with instructions, model selection, permitted tools and execution limits |
| Tool | Registered operation with input/output schemas, effect/permission policy and an executable implementation |
| Graph / Super-agent | Agents, tools and human-input steps connected by dependencies; a super-agent exposes a graph through a reusable input/output contract |
| Skill | Markdown instructions and supporting resources, optionally including scripts |

There is no separate public `Action` or `Func` type in the new authoring API.
A tool can be chosen by an agent or explicitly invoked as a graph node. Both
paths use the same registry, validation, approvals, execution and receipts.
Implementations may be native C++, an xlang3 Python callable or an MCP adapter.
An MCP client is a connector to tools, not an additional execution-node type.
Tools do not own xMind's reasoning loop; an external tool may internally use a
model, so external implementations are not advertised as universally LLM-free.
Inline helper functions remain ordinary code. Reusable/scheduled operations
are registered tools. Simple input/output mappings can be graph bindings.

Two authoring styles resolve to the same validated runtime definitions:

1. **Declarative:** YAML defines agents, tool bindings and graphs; Markdown
   provides instructions/skills; optional `.py` modules implement custom tools
   or behavior through xlang3.
2. **Programming:** Python executed by xlang3 calls the native xMind API to
   construct/register agents, tools and graphs and load configuration/Markdown.

The styles can mix. Code registration supplies definitions and callables; it
does not bypass admission, take ownership of SQLite or replace scheduling.
Unrestricted script code is a trusted extension until stronger containment is
implemented. Neither embedded execution nor shared-memory IPC is an OS sandbox.

The current authoring foundation accepts bounded YAML/JSON graph catalogs,
reusable bounded YAML/JSON named agent catalogs, YAML or JSON global
instruction records, and plain Markdown instruction files through offline
native administration. Trusted xlang3 `configure(xmind)` programs can declare
named agents and graph catalogs through a narrow native API; the same existing
catalog validators and xlang3 SQLite stores remain authoritative. Named
definitions bind prompt guidance, optional model and optional tool allowlist to
graph nodes; import pins each resolved definition into the immutable graph
revision. These records are persisted through embedded xlang3 SQLite.
Revisioned single-agent session selection and workspace Markdown skill
catalogues are implemented in the shared backend and thin clients. Optional
per-agent native tool allowlists now narrow the already-authorized native/MCP/A2A
catalogue and are pinned through named definitions, graph revisions and session
snapshots. The programming API declares catalogs and binds trusted xlang3 tool
functions into the existing native tool loop. A fuller API for composing
callable agent behavior and dynamic runtime services remains incomplete. See
[authoring formats and current coverage](authoring.md).

## Planning, AgentFlow and scheduling

Planning decides **what** steps are needed. AgentFlow enforces the execution
model and graph dependencies. Scheduling decides **when and where** eligible
work runs. Graph readiness and scheduling are distinct responsibilities, even
when their initial implementation is inside the same native service.

The native execution platform shares one bounded root admission budget between
standalone agents and graph runs. Queued, running and durably paused roots all
hold a reservation; terminal runs and rejected admissions release or avoid
reservations. The new native source shares execution slots across the agent and
graph worker loops, using the platform's configured worker count as the combined
root concurrency limit. Installed54 uses a cancellable worker-ready FIFO and has
passed its complete154-contract gate and existing-profile adoption.
[Full qualification](evidence/shared-root-dispatch-full-20261019.json).

Installed55 reserves that FIFO rank when a runnable root enters
either service's queue, so an older local-backlog root cannot be overtaken by a
later root from the other service. Queue tickets are one-shot, withdrawn on failed
admission/cancellation/shutdown, and renewed on a runnable wake. A human pause
retains durable admission but releases execution. The actual mixed-service
ordering regression fails against installed54 production bytes and passes with
the correction, alongside seven related contracts. The complete154-contract local
gate passes in1,061.07seconds; installed55 adoption passes while hosted acceptance remains pending.
Worker loops/ceilings remain separate; a strict
FIFO head can wait for its service worker rather than use another service's idle
worker. This does not implement a unified work-conserving pool or process placement.
[Focused red/green scope](evidence/shared-backlog-order-focused-20261019.json).
[Complete local gate](evidence/shared-backlog-order-full-20261019.json).

The coding default is a single-agent model/tool loop. Explicit planning is
optional; predefined graphs do not require a planning LLM call. A planner can
propose a structured graph, which the runtime validates before scheduling.
Agent nodes can use their own model/tool loops. Tool nodes and dependency
transitions require no model inference merely to advance the graph. Plans may
be revised only through validated native transitions and recorded revisions;
they never grant tool-effect approval.

Planning has no unconditional token-saving promise. It adds inference work,
and multiple agents can duplicate context. Savings require avoiding enough
unnecessary model turns, duplicated data or retries. Compare actual task
success and total input/output/cache tokens across the root, planner and all
children. A graph with many agent nodes may consume more than one agent.

## One product executable, separate execution lifetime

The product entry point is `xmind.exe`:

| Mode | Responsibility |
| --- | --- |
| `xmind serve` | Persistent authenticated backend, called xMind Runtime |
| `xmind` | Console client; discover/start or attach to a selected local runtime |
| `xmind worker` | Private xlang3 worker for one trusted registered-tool invocation |

One executable can run in multiple processes. Closing a console/view detaches
observation; it does not cancel agent execution. The runtime owns durable
sessions, events, approvals and SQLite. Client discovery/start must validate
workspace, connection, process identity and protocol compatibility; it must
not silently create an empty profile or replace an existing owner. Existing
native ownership and effect-reconciliation rules continue to apply. There is
one current package/profile format, with no old-format migration. Internal administrative/schema
entry points may also be modes of this executable; they do not create a second
agent engine. xlang3 DLLs, native packages and pure-source libraries remain
runtime dependencies; one product executable does not mean a dependency-free
single-file distribution.

Webpage and VS Code use authenticated HTTP commands and an SSE event feed.
Commands include admission, configuration, approvals and cancellation. Streamed
events include response text, tool activity, lifecycle, checkpoints and actual
usage. Reconnection resumes committed event cursors without replaying a run.
The webpage has a browser-facing access adapter; it does not invoke the console.
Browser-origin/session enforcement stays in that adapter. Streaming disconnect
does not cancel work. Expired authentication requires reconnecting explicitly.

Local native console/host/worker transport may use xlang3 shared-memory IPC,
after the actual SDK API and cross-process ownership rules are verified. It
carries the same versioned command/event contracts and authorization as HTTP.
Browser JavaScript cannot directly attach to that native transport; the browser
adapter bridges it if selected. No performance claim is made before measurement.
[The pinned SDK passed its official script/parallel/native IPC smoke, and the
local registered-tool worker path now passes an actual xlang3 tool call.](native-ipc-worker-design.md)
Durable agent workers and remaining failure/lease acceptance remain separate.

Tool worker mode gives a single invocation a child-process crash boundary and
terminates it on cancellation; it does not impose OS resource limits or
sandboxing. Scheduling, approval authority, durable state and effect receipts
remain runtime-owned.
Worker loss requires recorded reconciliation; an unknown effect cannot be
silently retried. Worker placement and sandbox policy are separate settings.

## Scope and current evidence

OSS retains local SQLite, CLI, browser and VS Code, native models, MCP/A2A and
the generic Local/Nexus connection/worker protocol. Nexus privately implements
teams, PostgreSQL, distributed coordination, WebRTC/signaling and Electron.
Clients and local workers never access Nexus PostgreSQL directly.

Current native components include single-agent coding/tool execution, registered
agent/tool/human graphs, bounded dynamic dependency planning, provider adapters,
encrypted configuration and embedded-xlang3 SQLite. See the scoped evidence in
[milestones.md](milestones.md); this is not a claim of complete feature parity.

The primary native `xmind` target provides real `serve`, console, `admin`
and private `schema-worker` modes using the same handlers. Dedicated contracts
run actual patches, graphs and MCP through that executable. Without `--port`,
the console now discovers or starts a protected native workspace profile;
`--port` explicitly attaches to an operator-selected backend.
[Native profile startup and restart](local-profiles.md) passed with the complete
[109-contract gate](evidence/native-local-profile-local.json).
[Two real provider console runs](evidence/native-unified-cli-live.json) separately
verify literal multiline patches, denial, disk receipts, actual usage and exact
restart history. These are local native checks, not installed UI acceptance.

The package contains one `xmind.exe` product executable plus xlang3 dependencies.
The fresh 0.1.5 installation passed file-integrity checks; it still carries the
earlier accepted runtime and managed launcher. Its rendered/sidebar acceptance
and adoption of the native profile controller remain pending. Preserve existing
installations; no legacy-format or existing-profile migration is required.

The native-controller editor adapter and browser enrollment now passed real
native integration and the full hosted 110-contract gate at `689404e`.
Its packaged bytes were independently checked. Fresh installed/rendered coding
acceptance remains pending; earlier installed previews are separate checkpoints.
[Exact gate and package scope](evidence/native-view-hosted-689404e-package.json).

The production browser and VS Code controllers now pass local native HTTP/SSE
integration through real pause, incremental resume, reconnect, detach and an
approved file effect; exact scope is recorded in
[native event-feed evidence](native-event-stream.md). A fresh rendered
installation and live-provider SSE remain separate acceptance. Pending delivery
also includes durable agent-level worker placement, reusable nested
super-agents, shared scheduling, Local/Nexus connection integration and
remaining provider/coding parity.

The [committed event transport](native-event-stream.md) passed its full native
gate at `7a148ff`, then the production reader, browser subscription, active-feed
replay/capacity/revocation and the native VS Code host adapter passed together
in `native_local_view_contract` at `4701255`. Rendered/installed acceptance
remains a distinct requirement.

Required acceptance covers the actual consolidated server/console, existing
native contracts, client adapters and a fresh installed profile, then stream
disconnect/replay, equivalent declarative/code registration and real agent
worker crash/cancellation/effect reconciliation. A passing parser or dispatcher fixture
does not establish those product boundaries.

The next shared physical worker component, RootWorkExecutor, is implemented and
passes its focused actual-thread lifecycle contract. Its staged FIFO tickets
reserve rank before durable admission and commit after acceptance; rollback
withdraws undispatched work, while owner/pool closure stop-requests and drains
committed retirement actions. Services must observe action futures and retain
their own fault/paused-root policy. This earlier component-only pass did not qualify integration. The subsequent
shared-physical-root integration receipt records the current focused service scope.
See [focused component evidence](evidence/shared-root-worker-component-20261019.json).
