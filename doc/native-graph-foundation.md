# Native graph foundation

Subsequent native [child-execution persistence and shared-engine integration](native-graph-children.md) is verified separately. The foundation checkpoint's earlier scope below is retained as historical evidence; the public graph scheduler, coordinator checkpoint transactions and client workflows remain pending.

Release compilation and all **38 native contracts passed locally**: [complete build/CTest output](evidence/native-graph-foundation-local-build-ctest.log), [source-tree/binary provenance](evidence/native-graph-foundation-local-provenance.json). The first complete run passed the C++ domain/storage checks but its server fixture omitted the required authentication token: [original first result](evidence/native-graph-foundation-local-first-result.log). The fixture now supplies a private temporary token and the guarded complete rerun passed. No authentication requirement was relaxed.

This checkpoint implements native planning, coordination/checkpoint rules and stored definitions. Coordinator completion and human-input values in its tests are synthetic. Actual xlang3/SQLite configuration, update faults, reopen, native admin import and backend ownership are verified. Agent/tool graph execution, concurrent worker execution, root/child persistence and graph HTTP/CLI/view workflows remain unimplemented. The existing single-agent engine remains the runnable execution path.

`GraphPlan` accepts a strict JSON object with 1–100 `nodes`. Agent nodes specify `prompt` and optional `model_id`; tool nodes specify `tool` and an `arguments` object; human nodes specify `prompt`. Each node has a unique bounded ID and optional `depends_on` and `when`. Unknown fields, duplicate keys, invalid dependencies/references and cycles are rejected. Specifications are bounded at 256 KiB and parsed with the existing native strict JSON depth/UTF-8 rules. DAGs match the retained prototype's current graph scope; bounded loops, A2A and script node contracts remain required by the full goal.

The coordinator is owned by one scheduler thread. Independent dependency-ready branches can both be claimed; this is a readiness/state contract, not evidence of parallel worker execution. Joins wait for every dependency. `when` compares an owned dependency output selected by string keys/nonnegative array indices with a typed JSON `equals` value. False conditions and skipped dependencies propagate skipping. Missing output paths fail resolution rather than fabricating values.

Tool argument objects can contain `{"$ref":{"node":"dependency","path":["items",0,"path"]}}`. References must name declared dependencies and cannot include additional literal fields. Resolution preserves JSON types and occurs before changing the node to running; resolved arguments must remain an object within 64 KiB. Agent nodes receive owned dependency-output JSON for the future dispatcher to pass as task data. No reference can grant tool access: the native tool registry and effect approval/precondition contracts remain authoritative.

Human nodes enter `waiting_human`, not running. Only the future authenticated controller path may provide their input. The current domain method is an internal C++ transition, not a server endpoint. A graph decision such as `approved:true` is data/control flow and does not grant a native effect permission.

Checkpoints bind the complete immutable specification and every node state/output. Restore verifies node identities, dependency/condition order, output bounds and human-node state. Completed outputs and human waits survive. A previously running node becomes `uncertain`; the coordinator halts admission and cannot restart that node. Failed, uncertain or cancelled nodes halt further dispatch. A cancellation request only retires pending/human-wait nodes; it does not claim that running work stopped. Per-node outputs are capped at 64 KiB, aggregate outputs at 512 KiB and serialized checkpoints at 1 MiB. Large result artifacts and reconciliation remain pending.

`GraphCatalogStore` stores trusted definitions in the information repository's `native-graphs/catalog` record through embedded xlang3. It accepts at most 16 definitions, assigns backend catalog/per-definition revisions, preserves unchanged canonical specifications, rotates changed definitions, and permanently retires removed IDs within a 4096-ID budget. Malformed/spoofed/corrupt records fail before replacement; a failed actual SQLite update preserves the previous record. This is a single configuration-owner startup/offline contract, not a concurrent runtime update API. Definitions are non-secret configuration; credentials remain in their separate encrypted repository. Checkpoint serialization is implemented, but runtime checkpoints are not yet committed by a graph repository.

Offline configuration import is available while the selected backend is stopped:

```powershell
.\build\native\Release\xmind_admin.exe --db STATE.sqlite --modules MODULES --stdlib LIB_SOURCE import-graphs graphs.json
```

```json
{"graphs":[{"id":"review","spec":{"nodes":[{"id":"inspect","type":"agent","prompt":"Inspect the requested work and report observed evidence."}]}}]}
```

The command returns catalog/definition revisions and node counts, with `execution_available:false`. Importing a definition does not execute it. The actual backend lease rejects an offline import while a server owns that database. No graph run button or simulated execution route was added.

The next integration must introduce root/child execution and conversation records behind the C++ repository, with all database I/O through xlang3. The existing one-active-run session rule must apply to root runs; graph nodes are child executions with isolated model conversations. They must not be submitted as competing ordinary runs on the root session. Node admission/checkpoint/event updates must commit together, child agent nodes must invoke the shared native engine, and tool nodes must use the same registered handlers, permissions, guidance bindings and effect journals. Root cancellation must wait for observed child retirement; owner recovery must preserve completed/human-wait work and quarantine interrupted effects. Only then can authenticated graph submit/inspect/resume, CLI and sidebar workflows be enabled and tested with actual model/tool work. Bounded loops, delegation, live providers/peers and the remaining full platform/parity scope stay required.
