# Native graph service and client controls

The C++ execution platform owns standalone agents and graph roots independently of connected views. A graph is an immutable, revisioned backend catalog entry. HTTP callers select a registered graph; they cannot upload a plan, choose a controller actor, or mutate execution states. Backend and console paths are implemented. The VS Code adapter now includes workflow selection, human input and separate child activity; actual IDE graph acceptance and installation remain pending. See [sidebar workflow scope](vscode-graph-workflows.md).

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

| CLI command after `xmind_cli PORT` | Backend operation |
| --- | --- |
| `graphs` | GET `/v1/graphs`: registered IDs, revisions, node counts and executable flags |
| `graph-run SESSION GRAPH REV PROMPT [MODEL]` | POST `/v1/graph-runs`: admit a registered graph; return the actual queued root |
| `graph ROOT` | GET `/v1/graph-runs/ROOT`: actual run, immutable specification/input and checkpoint revision |
| `graph-children ROOT` | GET `/v1/graph-runs/ROOT/children`: actual owned child runs |
| `graph-child-history ROOT CHILD` | GET `/v1/graph-runs/ROOT/children/CHILD/history`: isolated child transcript; a foreign child is rejected |
| `graph-events ROOT [AFTER]` | GET `/v1/graph-runs/ROOT/events?after=CURSOR`: persisted root and child events |
| `graph-input ROOT NODE REV JSON_FILE` | POST `/v1/graph-runs/ROOT/human/NODE`: typed input with expected checkpoint revision |
| `cancel ROOT` | POST `/v1/runs/ROOT/cancel`: cancel through the owning execution service |

For a waiting `choose` node, write `{"path":"src/main.cpp"}` to a JSON file and pass its path and the latest checkpoint revision to `graph-input`. HTTP clients can provide an `input` object or an `input_json` string, exclusively. The sidebar sends the original JSON text, preserving duplicate keys for native rejection rather than silently collapsing them in JavaScript. Duplicate keys, excessive nesting, oversized input, stale revisions and caller-supplied actors are rejected. Human input is data; it does not grant file or command effects. A partial answer retains a durable pause when no executable branch becomes ready. An answer that releases a ready branch resumes that work while retaining other pending human decisions.

The root ownership pool counts queued, running and paused roots toward `--queue-limit`. Standalone-agent and graph pools each use bounded workers/capacity; this is not yet one combined scheduling budget. Paused graph ownership blocks provider replacement, preserving the model/credential context required by its remaining nodes. Closing a service preserves an already durable human pause, cancels accepted but undispatched roots, stops active children and joins workers. On startup, repository recovery quarantines interrupted executable work; the service adopts only consistent human pauses and never replays queued/running effects.

After a runner/persistence fault the service stops admitting and dispatching work and reports its graphs unavailable. It preserves unfinished effect journals and actual file bytes rather than inventing a terminal result. Restart applies the existing uncertain-effect recovery rules. Root completion records observed outputs with `source: graph_join`; child provider usage remains on actual child events/transcripts, without fabricated aggregate tokens.

Verification uses compiled C++ services, the actual CLI, HTTP sockets, xlang3 SQLite, workspace file reads/writes and injected SQLite trigger failures. The model protocol peer supplies explicitly synthetic replies/counts. This does not establish live-provider graph acceptance, team authorization, PostgreSQL, persisted total graph deadlines, large result artifacts, loop/script nodes, direct MCP graph aliases, A2A execution or completed editor graph workflows. Remaining platform scope is unchanged.
