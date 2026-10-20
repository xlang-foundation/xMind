# Native graph service and client controls

The C++ execution platform owns standalone agents and graph roots independently of connected views. A graph is an immutable, revisioned backend catalog entry. HTTP callers select a registered graph; they cannot upload a plan, choose a controller actor, or mutate execution states. Backend and console paths are implemented. The VS Code adapter now includes workflow selection, human input and separate child activity; actual IDE graph acceptance and installation remain pending. See [sidebar workflow scope](vscode-graph-workflows.md).

Checkpoint `bd059a46781292640d95943e00b0d86a4891936c` passed **44 native and 49 extension contracts** in [hosted CI](https://github.com/xlang-foundation/xMind/actions/runs/37682783904): [CTest](evidence/native-graph-service-hosted-ctest.log), [original job/TAP](evidence/native-graph-service-passing-ci-job.log), [provenance](evidence/native-graph-service-hosted-provenance.json). Subsequent sidebar/task-control sources and local IDE observations retain their own separate verification scopes.

Start a Windows native server with the trusted catalog file:

```powershell
$env:XMIND_AUTH_TOKEN = '<private server access token>'
./Tools/agentflow.ps1 -Action Serve -Database .agentflow/native/state.sqlite -Workspace D:/project -GraphsConfig D:/config/graphs.json
```

The file uses the existing catalog import format, for example:

```json
{
  "graphs": [
    {
      "id": "review.read",
      "spec": {
        "nodes": [
          {"id": "choose", "type": "human", "prompt": "Choose a workspace file"},
          {"id": "read", "type": "tool", "tool": "read_file", "depends_on": ["choose"], "arguments": {"path": {"$ref": {"node": "choose", "path": ["path"]}}}}
        ]
      }
    }
  ]
}
```

The catalog assigns revisions; clients obtain the current revision from `graphs`. Tool/human graphs can run before a provider is configured. Agent nodes require the existing configured native model adapter. Direct workspace tools require the server's verified workspace; writes additionally require approved edits and a separate approval of each actual proposal. Stored command profiles stay inactive on a model-free owner without a workspace.

Graph runs may attach workspace-relative file references. The owner requires the current workspace identity, authority and membership revision, reads each file through its registered folder, and persists bounded UTF-8 snapshots into the graph root input before queuing execution. Each graph agent receives those snapshots as explicitly untrusted file content; direct graph tool nodes do not treat attachments as tool arguments. A graph without agent nodes rejects file attachments. Browser, VS Code and CLI graph admission share this contract.

| CLI command after `xmind_cli PORT` | Backend operation |
| --- | --- |
| `graphs` | GET `/v1/graphs`: registered IDs, revisions, node counts and executable flags |
| `graph-run SESSION GRAPH REV PROMPT [MODEL] [--file PATH] [--folder-id ID]` | POST `/v1/graph-runs`: admit a registered graph; optional files are native snapshots passed as untrusted context to graph agent nodes |
| `graph ROOT` | GET `/v1/graph-runs/ROOT`: actual run, immutable specification/input and checkpoint revision |
| `graph-children ROOT` | GET `/v1/graph-runs/ROOT/children`: actual owned child runs |
| `graph-child-history ROOT CHILD` | GET `/v1/graph-runs/ROOT/children/CHILD/history`: isolated child transcript; a foreign child is rejected |
| `graph-events ROOT [AFTER]` | GET `/v1/graph-runs/ROOT/events?after=CURSOR`: persisted root and child events |
| `graph-watch ROOT [AFTER]` | Live NDJSON observation of persisted root and owned-child events; source prepared, compiled validation pending |
| `graph-input ROOT NODE REV JSON_FILE` | POST `/v1/graph-runs/ROOT/human/NODE`: typed input with expected checkpoint revision |
| `graph-tree-input ROOT OWNER NODE ROOT_REV OWNER_REV JSON_FILE` | POST `/v1/graph-runs/ROOT/invocations/OWNER/human/NODE`: descendant input bound to both observed checkpoint revisions |
| `cancel ROOT` | POST `/v1/runs/ROOT/cancel`: cancel through the owning execution service |

For a waiting `choose` node, write `{"path":"src/main.cpp"}` to a JSON file and pass its path and the latest checkpoint revision to `graph-input`. HTTP clients can provide an `input` object or an `input_json` string, exclusively. The sidebar sends the original JSON text, preserving duplicate keys for native rejection rather than silently collapsing them in JavaScript. Duplicate keys, excessive nesting, oversized input, stale revisions and caller-supplied actors are rejected. Human input is data; it does not grant file or command effects. A partial answer retains a durable pause when no executable branch becomes ready. An answer that releases a ready branch resumes that work while retaining other pending human decisions.

For a nested human node, obtain the ultimate root checkpoint and its actual descendant graph owner from the immutable tree observation. When health advertises `graph_tree_human_input`, send both `expected_checkpoint_revision` and `expected_owner_checkpoint_revision` to the descendant route, with exactly one `input` object or raw `input_json` string. The owner must be paused and belong to that root/session. `graph-tree-input` accepts the same values in command mode. Root-only `graph-input` cannot address descendant nodes. Caller-supplied actors and stale/foreign ownership are rejected. Context-enabled nesting and shared durable budgets across pauses are still unfinished; this candidate is not installed. Current verification is in [validation status](VALIDATION_STATUS.md).

The root ownership pool counts queued, running and paused roots toward `--queue-limit`. In `ExecutionPlatform`, agent and graph services share a root admission budget sized to the queue limit plus their worker slots, so combined accepted work stays bounded and durable human pauses keep their reservations. The services still dispatch through separate bounded worker loops; global ordering and fairness across agent and graph work are not yet unified. Paused graph ownership blocks provider replacement, preserving the model/credential context required by its remaining nodes. Closing a service preserves an already durable human pause, cancels accepted but undispatched roots, stops active children and joins workers. On startup, repository recovery quarantines interrupted executable work; the service adopts only consistent human pauses and never replays queued/running effects.

After a runner/persistence fault the service stops admitting and dispatching work and reports its graphs unavailable. It preserves unfinished effect journals and actual file bytes rather than inventing a terminal result. Restart applies the existing uncertain-effect recovery rules. Root completion records observed outputs with `source: graph_join`; child provider usage remains on actual child events/transcripts, without fabricated aggregate tokens.

Verification uses compiled C++ services, the actual CLI, HTTP sockets, xlang3 SQLite, workspace file reads/writes and injected SQLite trigger failures. The model protocol peer supplies explicitly synthetic replies/counts. This does not establish live-provider graph acceptance, team authorization, PostgreSQL, persisted total graph deadlines, large result artifacts, loop/script nodes, direct MCP graph aliases, A2A execution or completed editor graph workflows. Remaining platform scope is unchanged.
