# Native backend instruction configuration

This component adds a trusted instruction supplement to the shared native single-agent runner, for general and coding tasks. Release compilation and all **36 native contracts passed locally**: [original complete build/CTest output](evidence/native-agent-instructions-local-build-ctest.log), [source-tree/binary provenance](evidence/native-agent-instructions-local-provenance.json). Inference is synthetic. The currently installed preview remains on the earlier verified 35-native/40-extension process bundle; this expanded checkpoint still awaits isolated CI. No configured model, instruction policy or response is seeded in that preview.

The C++ `AgentInstructionStore` persists one versioned record through embedded xlang3 in the existing information repository. A trusted JSON file accepts only `instructions`, with at most 32 KiB of UTF-8 text and no NUL. Duplicate keys, unknown/backend-owned fields, invalid text and excessive input are rejected before writing. The backend assigns positive revisions; byte-identical imports preserve them, changes rotate them, and an empty supplement retains the core instructions. Invalid stored versions/revisions and revision exhaustion fail closed without replacing evidence. A failed SQLite update must preserve the earlier record.

Example non-secret configuration:

```json
{"instructions":"You are the project maintenance agent. Explain observed results clearly and use approved tools for repository work."}
```

While the backend is stopped:

```powershell
.\build\native\Release\xmind_admin.exe --db STATE.sqlite --modules MODULES --stdlib LIB_SOURCE import-instructions instructions.json
```

Alternatively import at startup with `xmind_server --instructions-config instructions.json` or `Tools/agentflow.ps1 -InstructionsConfig instructions.json`. The next server instance loads the saved record without requiring the source file. Configuration is a startup snapshot; there is no HTTP or model mutation route. The existing database owner lease prevents an offline importer from changing a running backend's snapshot.

The supplement is appended to the backend's existing core system text. The combined text is capped at 64 KiB and sent through the configured native provider. It does not modify C++ tool permissions, executable/workspace bindings, one-use decisions or effect journals. Text requesting automatic approvals cannot grant them. Credentials remain in their separate encrypted repository; instruction text is intended for the model and stored as general information, so it is not a secret store.

The authenticated `GET /v1/agent/instructions` and `xmind_cli PORT instructions` return only `revision`, custom `byte_count`, `scope:server` and `runtime_state:startup_snapshot`. Unknown query parameters are rejected. An imported policy emits one `agent.instructions` metadata event before the run's first provider request, with the same revision/count. Neither discovery nor that event exposes the instruction text. Revision zero means no imported supplement, while the core text remains configured normally.

Passed contracts cover real xlang3/SQLite persistence, Unicode and byte bounds, backend revisions, spoofed/malformed imports, an actual failed update, reopen and corrupt/exhausted records. Actual admin/server/model/CLI checks inspected the transmitted core/supplement, immutable owner lease, metadata privacy/restart and continued native denial despite an approval-bypassing fixture instruction. Inference is explicitly synthetic. The first local integration attempt hit a Windows `EBUSY` open on its copied executable before mutation: [original first result](evidence/native-agent-instructions-local-first-result.log). The fixture now retries only that no-write open within five seconds; actual mutation and stale-binding/no-dispatch assertions remain strict. The full 36-contract rerun passed. This does not establish live-model behavior or populated editor configuration.

Repository root discovery and explicit scoped reads have a separate [native implementation checkpoint](native-repository-instructions.md), pending execution. Automatic nested scope enforcement, named agent presets, graph-node settings, skills/commands, prompt snapshot archives and context compaction remain required. This record keeps the latest supplement only; its per-run metadata is not an archive of previous prompt text. The component does not establish full instruction/preset or OpenCode parity.
