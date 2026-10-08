# Native implementation direction

Current scope: the [xMind OSS specification](../doc/architecture.md) excludes team-server features, PostgreSQL, WebRTC and the standalone Electron IDE. Those belong to Cantor Nexus; earlier roadmap language does not make them OSS completion requirements.

The user requires the core platform to be implemented in C++. OpenCode is a feature reference only; LiteLLM is a provider/model capability reference only. Neither implementation is embedded or executed as AgentFlow. This supersedes the earlier Python/FastAPI implementation plan.

The user confirmed the mixed design: C++ core with embedded xlang3 for scripts, skills and pure-Python libraries, and authorized continued work after the diagram and companion design were produced. The architecture diagram is `../doc/architecture.svg`.

The companion design in `../doc/architecture.md` specifies component ownership, shared execution contracts, graph/root-run relationships, scripting value lifetimes and trust boundaries, cancellation and recovery semantics. Read it with the diagram before implementing the native components.

## Platform ownership

| Component | Implementation and responsibility |
| --- | --- |
| Native execution engine | C++ model/tool loop, cancellation, deadlines, bounded retry and context management; reused by single agents and graph agent nodes |
| Session/event repository | C++ contracts performing all SQLite operations through embedded xlang3, with owned buffers, explicit transactions, run ownership, monotonic events, checkpoints and restart recovery |
| Graph scheduler | C++ validation, dependencies, conditions, parallel branches, bounded loops, human pauses and durable effect/checkpoint handling |
| Tools and permissions | C++ workspace tools, search/edit/patch, process/PTY, Git, snapshots and explicit policy/approval enforcement |
| Models/providers | C++ adapters for provider-specific authentication, wire protocols, streaming, tool calls, reasoning and multimodal inputs; LiteLLM coverage reference |
| Shared service | Native HTTP/JSON/SSE transport with versioned endpoints, authentication, event replay and graceful shutdown |
| MCP/A2A | C++ clients and servers over approved native transports, sharing execution, tool permissions and persistence |
| CLI | Native C++ client and interactive interface over the same backend |
| VS Code | Thin JavaScript/TypeScript extension required by the editor host; displays native-backend state, context, diffs and approvals |
| xlang3 | Embedded through its supported native SDK for user scripts, skills and pure-Python libraries. No CPython execution |

Native infrastructure libraries such as a JSON parser, SQLite and HTTP/TLS libraries are allowed. Select and pin their versions and licenses after checking existing dependencies and Windows build support. Do not rewrite cryptography or a database engine as application code. No native dependency selection has been finalized by this document.

## Existing source and migration

The retained `Core` has native graph, callable, planning and session concepts. Its `BEGIN_PACKAGE`/`APISET`, `X::Value`, `X::XLoad` and headers compile against `ThirdParty/xlang/Api`. The root CMake build includes that old runtime. These interfaces are not compatible with xlang3 simply by renaming the dependency path. Inspect behavior worth retaining, then port through xlang3's supported native SDK with explicit ownership/lifetime contracts.

The `agentflow` Python package, Python launch/setup scripts and their checks remain historical prototype evidence. Do not continue adding production functionality there or count their passing checks toward C++ delivery. Retain them until native contracts replace their useful scenarios; no destructive cleanup is authorized by the architecture change.

`Documents/OPENCODE_API_INVENTORY.json` maps all 136 pinned API operations to required behavior and source contracts. Twelve partial equivalents point at the historical prototype, not native completion. The inventory is only the API portion: tools, plugins, context management and editor workflows require additional source-level comparison. Do not transplant OpenCode code.

## Implementation sequence

1. Establish an independent native CMake target and contract tests, keeping the old runtime build out of the new target. Fix supported xlang3 SDK selection and Windows native dependency linking without modifying the other xlang3 checkout.
2. Implement the native session/run/event repository with transactional state transitions, atomic event publication, restart recovery and concurrent-client tests. Add typed execution and provider/tool contracts.
3. Implement native HTTP service and CLI for the same versioned session/run contract. Validate cross-client state and reconnect before migrating the extension connection.
4. Implement the model/tool engine and native provider adapters, then permissions, coding tools, snapshots and diff review. Validate real provider and coding tasks, not only test doubles.
5. Add native protocol interoperability and graph execution with durable checkpoints, then complete CLI/editor and the pinned parity requirements.

Full C++ implementation remains outstanding. The native SQLite store, independent CMake target and transaction/concurrency contract tests are prepared but uncompiled and unverified. Build and test instructions are in `../doc/native-development.md`. Existing xlang3 benchmarks in the other checkout are live; defer competing resource-heavy builds until they finish. Native AES-GCM work remains separate and unverified. SDK-specific compatibility work no longer gates the C++ core, but confirmed missing APIs must still be discussed before changing xlang3 native capabilities.
