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
reimplementing cryptography. Production SQLite I/O still goes through xlang3;
the direct-SQLite reference is not production persistence. See
[native development](native-development.md) for build selection and prerequisites.

## Current source and verification

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

The committed common Gemini gateway/history source
`75f45f0a036f1ffbab8c4b157df364f3697b52a0` awaits hosted validation. Its local
compiled baseline passed **61 native, 91 extension and 17 browser contracts**
and native/browser integration. Callback assertions and a large function-response
correction added after compilation remain excluded from that pass claim.
[Exact local scope](evidence/native-gemini-history-local-provenance.json).
The newer Gemini AgentRunner contract increased the native manifest to **62**;
all 62 passed locally in **100.44 seconds**, including those earlier exclusions.
Actual native file reads and signed history continuation after xlang3 SQLite
reopen passed with independent synthetic provider replies. Browser/native
integration passed again against the rebuilt backend; current thin-client
sources retain their 91/17 pass. [Current evidence](evidence/native-gemini-agent-local-provenance.json).
Live Gemini inference and product enrollment remain incomplete.
The installed browser preview retains its separately verified native `19d69dd`
and view `6f32d215` snapshots. [Current validation](VALIDATION_STATUS.md).

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
