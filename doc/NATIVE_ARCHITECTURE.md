# Native implementation direction

The [xMind OSS specification](architecture.md) covers the local C++/xlang3
runtime, SQLite, single and graph agents, native provider adapters, MCP/A2A,
CLI, browser UI and VS Code. Local/Nexus connection profiles share a versioned
command/event protocol. Nexus owns the private team server, PostgreSQL,
WebRTC/signaling and standalone Electron IDE; these are excluded from OSS delivery.

C++ owns the platform. Embedded xlang3 runs scripts, skills and compatible
pure-Python source libraries; CPython execution is prohibited. OpenCode and
LiteLLM are behavior/capability references only. Their implementations and SDKs
are not the platform. The user confirmed this design after the
[architecture diagram](architecture.svg) and companion
[ownership/contracts specification](architecture.md) were produced.

## Platform ownership

| Component | Implementation and responsibility |
| --- | --- |
| Native execution engine | C++ model/tool loop, cancellation, deadlines, retry and context management; reused by single agents and graph agent nodes |
| Session/event repository | C++ contracts performing all SQLite I/O through embedded xlang3, with owned buffers, transactions, run ownership, monotonic events, checkpoints and recovery |
| Graph scheduler | C++ validation, dependencies, conditions, parallel branches, bounded loops, human pauses and durable effect/checkpoint handling |
| Tools and permissions | C++ workspace tools, search/edit/patch, process/PTY, Git, snapshots and policy/approval enforcement |
| Models/providers | C++ provider authentication, wire serialization, HTTP/SSE, tool calls, reasoning and multimodal adaptation; LiteLLM is the coverage reference |
| Local service | Native HTTP/JSON/SSE with versioned endpoints, authentication, event replay and graceful shutdown |
| MCP/A2A | C++ clients/servers sharing execution, tool permissions and persistence |
| CLI | Native C++ client and interactive interface over the same backend |
| Browser and VS Code | Thin view/host adapters display backend-owned sessions, models, context, metrics, diffs and approvals |
| Connection profiles | OSS Local/Nexus protocol and local worker binding; private Nexus owns team policy, shared storage and distributed scheduling |
| xlang3 | Supported embedding/native SDK for database I/O, scripts, skills and pure-Python libraries; no CPython interpreter or native extension fallback |

Native infrastructure dependencies are pinned by the current build and license
inventory. C++ uses native HTTP/TLS and credential protection rather than
reimplementing cryptography. Production SQLite I/O goes through xlang3;
the superseded direct-SQLite implementation was removed. See
[native development](native-development.md) for build selection and prerequisites.

## Current source and verification

Native Responses reasoning continuation passed the exact ace2460 hosted gate: **71 native contracts in 171.52 seconds, 103 extension and 20 browser contracts**, with all 16 job steps, native/browser integration and 18 VSIX assets passing. Verified sources and artifact maps bind the completed-item continuation fix and its real native two-child read/join with synthetic provider replies. The local build was deferred by the benchmark guard. The exact native bundle is installed. One real OpenAI browser prompt completed two read-only native children and a joined parent response, with six owned model attempts, separate per-response metrics and zero effect operations. Immediate refresh retained the connection and selection. Upgrade and rollback checks passed at their separately documented scopes. [Compatibility and scope](native-responses-reasoning.md), [exact hosted evidence](evidence/native-responses-reasoning-hosted-provenance.json).

Ordinary Agent mode now offers native `delegate_tasks` for model-selected read-only workspace investigations. Actual child AgentRunners keep separate conversations and measured response usage/timing, share their parent's call/deadline limits, and return observed results before the parent continues or requests an approval-controlled effect. Schema v10 durably binds tasks, immutable presets, budgets and once-only settlements; recovery never replays children. CLI, browser and VS Code observe the same owned child histories and committed tree cursors.

For the preceding delegation checkpoint, the final guarded local **71-contract native gate passed in 142.36 seconds**, with an exact expected/registered/passed manifest, **45 frozen source hashes**, zero failures/skips and no exclusions. The delegation engine contract took **1.96 seconds**, actual HTTP/CLI/shared-controller acceptance **1.03 seconds**. **103 extension tests** and **19 browser tests**, fresh source-matched browser/native integration and 18-asset VSIX verification also passed. Provider replies, signatures and keys are explicitly synthetic; native agents, filesystem effects, permissions, xlang3 SQLite and transport are real. The [initial process-fixture positional-schema failure](evidence/native-delegation-initial-ctest.log) is retained separately; the corrected frozen source passed all 71 contracts.

Exact revision `e353a37799530a234a6fa13e51f61a5c52d3ae6a` subsequently passed its [hosted gate](https://github.com/xlang-foundation/xMind/actions/runs/37765666399): **71 native contracts in 175.51 seconds**, **103 extension tests in 2.9668103 seconds** and **19 browser tests in 1.426115 seconds**, plus native/browser integration and verification of all 18 required VSIX assets. The delegation engine took **4.88 seconds** and its HTTP/CLI contract **1.22 seconds**. All 16 job steps succeeded, with zero failures/skips and the exact complete native manifest. Downloaded artifact digests, exact source and runtime/stdlib pins were verified. [Exact hosted71 provenance](evidence/native-delegation-hosted-provenance.json), [raw hosted CTest](evidence/native-delegation-hosted-ctest.log), [original hosted job](evidence/native-delegation-passing-ci-job.log).

The preceding managed backend upgrade installed the exact hosted e353 bundle with schema v10; the later 7fe upgrade is recorded in the diagnostic scope. The actual upgrade preserved existing records, settings and browser access; its disposable schema 9→10→9 rollback fixture passed separately. The original e353 webpage exposed a classic-script name collision. A separate local browser repair passed **103 extension tests in 1.6557025 seconds**, **20 browser tests in 0.9090882 seconds**, native/browser integration and all 18 VSIX assets. The repaired page reused its cookie without key entry and restored prior history/metrics, the selected model and Agent mode. These frontend results are separate from the unchanged hosted 71/103/19 gate. [Upgrade evidence](evidence/native-delegation-upgrade-provenance.json), [acceptance scopes](native-delegation-acceptance.md).

The first e353 live `delegate_tasks` request failed with `responses_terminal_mismatch` before child admission: zero children, only the user history row and one parent model-budget attempt. That historical failure remains recorded; the later ace browser two-child join passed the separate acceptance above. Installed VS Code delegation acceptance remains unverified. Mutable dependency planning, skills, compaction, outbound A2A and full provider/coding parity remain incomplete. [Implementation and limits](native-delegation.md), [next native planning design](native-dynamic-plan-design.md).

New source connects pinned MCP tools directly to registered graph tool nodes,
including graphs with no configured model. A node binds its opaque alias to an
enabled server ID/revision and keeps literal arguments in an `arguments_json`
string; runtime rediscovery must contain that exact alias before a durable
approval can be proposed. Metadata and admission do not launch peers. Explicit
offline `discover-mcp SERVER_ID WORKSPACE` uses the native administrator's
stopped-backend lease to obtain aliases, schemas and untrusted descriptions,
rejecting credential reflection before public output. Actual tool dispatch
uses the existing approval, shared-resource claim and outcome journal.

The final guarded **69-contract native gate passed in 121.22 seconds**, with
the exact expected/registered/passed manifest, zero failures/skips and no
post-build exclusions. Fourteen frozen source hashes match the pre-build
capture. The new graph MCP contract took **5.59 seconds** and actual
HTTP/admin/CLI/view-adapter contract **2.31 seconds**. They verify raw
argument/reference preservation, no-model execution,
actual external writes/dependent reads, xlang3 SQLite reopen, stale paused
binding inspection/cancellation, reply-loss uncertainty and unrecorded-outcome
fail-stop. An acknowledged result over the graph's 64 KiB output limit fails
the child while retaining the succeeded operation; it cannot undo or replay
the external effect. This scope is separate from the completed Claude67 gate
below and adds no provider-parity claim. Fresh browser/native integration
passed against the rebuilt server and source-matched assets. Unchanged frontend
sources retain the exact fceb hosted **98 extension and 17 browser tests**;
those suites were not rerun for that local native gate. Exact source
`c4ec09fc3ee25c1b3a2bd9087b25f5af420ee616` subsequently passed hosted **69
native contracts in 184.36 seconds**, **98 extension and 17 browser tests**,
native/browser integration and 18-asset VSIX verification, with zero failures/skips
and all 16 job steps successful. Live direct-MCP graph and rendered IDE
acceptance remain pending. The initial HTTP start-guard failure is retained
separately from the final passing gate.
[Local69 provenance](evidence/native-graph-mcp-local-provenance.json),
[exact hosted69 provenance](evidence/native-graph-mcp-hosted-provenance.json),
[Direct MCP graph contract](native-graph-mcp.md).

The [delegation implementation](native-delegation.md) now applies transactional
v10 ownership/budgets for actual ordinary-Agent read-only leaves. Approved
parent effects remain controller-owned. The broader [design](native-delegation-design.md)
still requires dependency plans, revisioned replanning and remote acceptance.

The ordered-Claude-history checkpoint passed all **67 native contracts
locally in 117.65 seconds**, with an exact manifest, zero failures/skips and no
post-build exclusions. Ordered `anthropic_content` receipts preserve raw content
as a JSON string, accepted tool-input tokens/escaped keys, block interleaving and
opaque thinking/signature/redacted material. Strict receipt/DTO matching and
aggregate request bounds passed. The new helper contract took **0.50 seconds**;
the expanded actual AgentRunner contract took **0.57 seconds**, performing two
file reads and signed/interleaved xlang3 SQLite replay without repeated tools,
with unsigned-block rejection before effects.

Fresh browser/native integration passed against the rebuilt server. Frontend
sources match `ae7c4ba` and retain its recorded **98 extension and 17 browser
tests**; those suites were not rerun for that local native change. Exact source
`fceb50ba0493f645ee5f0a00d5400e0f103df231` also passed its hosted **67 native
contracts in 120.18 seconds**, **98 extension and 17 browser tests**,
native/browser integration and VSIX verification, with zero failures/skips and
all 16 job steps successful. Its history helper took **0.31 seconds** and
actual Claude agent contract **1.03 seconds**. This hosted result excludes the
new direct-MCP-graph source and its 69-contract gate above.
[Exact hosted67 scope](evidence/native-anthropic-history-hosted-provenance.json).
Thinking request controls, local cryptographic
verification, model/account acceptance, cross-wire signed conversion and full
parity remain incomplete; installed previews are unchanged.
[Local67 scope](evidence/native-anthropic-history-local-provenance.json),
[Ordered Claude history](native-claude-history.md).

The preceding Claude checkpoint passed the complete local **66-contract native
gate in 114.49 seconds**, with an exact manifest, zero failures/skips and no
post-build exclusions. Actual AgentRunner two-file execution, provider call/result
correlation, encrypted credential reuse, xlang3 SQLite history/reopen without
repeating tools, held-stream cancellation/recovery and second-tool-row SQL
rollback passed with an independent synthetic Messages peer. Shared native usage
events and saved responses preserve supplied uncached input/output/cache counters,
including zeros, without summed input or invented totals. The new agent contract
took **0.56 seconds** and the gateway adapter contract **0.23 seconds**.

The updated frontend suites passed **98 extension and 17 browser tests**, with
zero failures/skips; actual shared DOM fixtures cover live and saved metrics.
Fresh browser/native integration passed against the rebuilt server and matching
assets. Exact source `ae7c4ba07d59f57cbe393eff5a01b7d5186dfbc5` also passed
its hosted **66 native contracts in 131.92 seconds**, **98 extension and 17
browser tests**, native/browser integration and VSIX verification, with zero
failures/skips. Its Claude agent contract took **1.41 seconds**.
[Exact hosted66 provenance](evidence/native-anthropic-agent-hosted-provenance.json).
That gate excludes the newer ordered thinking/signature receipts, history helper
and raw replay source above. Live Claude inference, rendered
Claude IDE acceptance, all 21 pinned Anthropic models and full parity remain
incomplete. Installed previews are unchanged.
[Local66 provenance](evidence/native-anthropic-agent-local-provenance.json),
[Claude scope](native-claude-agent.md).

The original `Core` used the old xlang package/embedding ABI. That source and
its CLI, configuration, service, debug-plugin and legacy dependency assets were
removed. The Python/FastAPI `agentflow` prototype, dependent launcher/probes and
their status mappings were removed too. Git history retains their license and
source provenance. The user explicitly authorized this cleanup. Root CMake now
delegates to `Native/`, which uses xlang3's supported SDK and explicit lifetime
contracts. Maintained documents and inventories live in `doc/`.
[Cleanup scope](cleanup.md).

The exact cleanup source `dea588874e5a8c8e40bf1a158fa925520443c8a0` passed its
hosted **58 native, 88 extension and 17 browser contracts**, native/browser
integration and VSIX verification, with no failures/skips.
[Evidence](evidence/native-cleanup-hosted-provenance.json).
The later Gemini transport/replay source
`ddd1d3da087f9ca7f8b0b8d81705c82632e15094` passed its separate hosted **60
native, 88 extension and 17 browser contracts**, with the same integration and
package checks. [Evidence](evidence/native-gemini-transport-hosted-provenance.json).

The common Gemini gateway/history source
`75f45f0a036f1ffbab8c4b157df364f3697b52a0` passed its exact hosted **61 native,
91 extension and 17 browser contracts**, native/browser integration and VSIX
verification, with zero failures/skips. Its native gate took **121.84 seconds**.
The gateway/history receipts, native tool identities, supplied usage, stronger
callbacks and escaped large-response cases all compiled and passed there.
[Exact hosted scope](evidence/native-gemini-history-hosted-provenance.json).

The subsequent agent source `c9591fe79cad9a4253ac8088933f0c8a2848ded1` passed
all **62 native contracts locally in 100.44 seconds**. Actual native file reads
and signed history continuation after xlang3 SQLite reopen passed with
independent synthetic provider replies. Browser/native integration passed again
against its rebuilt backend; its unchanged thin clients matched the earlier
91/17 pass. [Local evidence](evidence/native-gemini-agent-local-provenance.json).
Its exact hosted gate passed **62 native, 91 extension and 17 browser contracts**,
native/browser integration and VSIX verification, with zero failures/skips. The
native gate took **128.80 seconds** and excludes newer catalogue/enrollment work.
[Hosted agent evidence](evidence/native-gemini-agent-hosted-provenance.json).

Enrollment source `2f5e0f05e9bfadcf86b5508863da0ec5a9e78cfe` passed its
exact hosted **64 native, 94 extension and 17 browser contracts**, native/browser
integration and VSIX verification, with zero failures/skips. Its native gate
took **158.09 seconds**, following the local 64-contract pass in **120.98
seconds**. Native Gemini catalogue discovery, encrypted profile enrollment,
owned-key file execution/cancellation/CAS rollback and signed xlang3 SQLite
replay are verified using synthetic peers. GenerateContent discovery does not
imply function-call capability: workspace mode requires backend-supported tool
policy, while text-only mode can retain unknown-tool models. Direct-SQLite
removal and shared `records.hpp` cleanup are included.
[Exact hosted enrollment scope](evidence/native-gemini-enrollment-hosted-provenance.json).

The preceding generic CLI source passed all **65 native contracts locally in 113.16
seconds**, with the exact manifest, zero failures/skips and no post-build
exclusions. Its CLI contract took **4.23 seconds** and exercises OpenAI, Claude
and Gemini profile catalogues, private environment-key setup, explicit shared
profile selection, safe native diagnostics and all-family public identity
reflection rejection. Signed Gemini text/history/usage, SQLite reopen with
encrypted credentials, stale discovery/admission without retry and failed-turn status preserved
through settings until actual recovery also passed. The C++ backend retains
credential binding, revision CAS and execution ownership; the CLI does not
silently rebind from metadata inspection or discovery. Fresh browser/native
integration passed again against the rebuilt server. Its unchanged frontend
sources retained the earlier verified **94 extension and 17 browser tests**;
those suites were not rerun for that CLI change.
[Local CLI scope](evidence/native-provider-profile-cli-local-provenance.json).
Exact committed CLI source `ac69c1f2a2c4a757b23d0d9da8b98d6188fab3a8` then
passed its hosted **65 native contracts in 149.37 seconds**, **94 extension and
17 browser tests**, native/browser integration and VSIX verification, with zero
failures/skips. Its CLI contract took **4.16 seconds**. The exact ac69 manifest,
runtime/stdlib/patch provenance and original log bytes are verified.
[Exact hosted CLI scope](evidence/native-provider-profile-cli-hosted-provenance.json).
This hosted65 result excludes newer Claude66 agent/metrics and Claude67 history
source. Live Gemini
inference and actual Gemini IDE interaction remain unverified.
[Provider setup](provider-setup.md).
The preceding `c4ec09fc` installed browser preview preserved prior records and settings;
one actual read-only OpenAI Agent task rendered history/metrics and survived
refresh. The original browser session required reconnect and its exact cause
remains unresolved. [Historical installed scope](preview-checkpoint-c4ec09fc.md).
The preceding exact e353 backend, separate local browser repair and failed first
live delegation attempt have their own [acceptance scopes](native-delegation-acceptance.md)
and [current validation](VALIDATION_STATUS.md).

[OPENCODE_API_INVENTORY.json](OPENCODE_API_INVENTORY.json) records **136** pinned
operations with acceptance requirements, **zero** removed-prototype mappings,
**23** partial native source candidates and **113** unmapped operations. No
operation is claimed as verified parity. Tools, plugins, context management,
providers and editor workflows require additional source/behavior comparison.
[Parity scope](PARITY.md).

## Remaining delivery work

1. Complete broad provider and model capability/authentication coverage through
   the shared native gateway, with actual agent/tool continuation, enrollment,
   discovery, persisted replay and live-provider acceptance.
2. Complete native coding workflows, including context compaction, instructions,
   skills/plugins, patch/diff review, snapshots, diagnostics and terminal behavior.
3. Finish MCP/A2A interoperability and remote delegation, graph behavior and
   recovery against independent peers and actual coding tasks.
4. Complete CLI, browser and right-sidebar acceptance over the common backend,
   preserving backend-owned permissions, history, metrics and recovery.
5. Implement the OSS Local/Nexus connection and local-worker contracts without
   moving private team services or PostgreSQL into this repository.
6. Establish release acceptance against the pinned parity inventory, reproducible
   builds and accurately scoped evidence. Contract counts are not completion
   percentages.

Recheck the sibling xlang3 benchmark guard before competing native builds.
Execute allowed Python sources with xlang3. Discuss exact missing native APIs
before changing xlang3 or selecting a workaround; isolate authorized fixes in
branches and preserve the sibling checkout/goal.
