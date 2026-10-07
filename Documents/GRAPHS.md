# Agent graphs

Graph orchestration source is implemented in `agentflow/graph.py` and exposed through the shared backend. Graph nodes reuse the same `Engine` for model/tool turns as standalone agents. Tool nodes use the same workspace permissions and optional MCP allowlists. Human nodes pause execution for explicit client input.

## API

- `POST /v1/graphs/runs`: `{ "session_id": "...", "spec": { "nodes": [...] } }`.
- `GET /v1/graphs/runs/RUN_ID`: run status, immutable graph specification and durable node checkpoints.
- `POST /v1/graphs/runs/RUN_ID/resume`: `{ "inputs": { "HUMAN_NODE_ID": { "approved": true } } }`.
- Existing run events/stream/cancel routes also apply to graphs.

Nodes have an `id`, `type` (`agent`, `tool`, `human`), and optional `depends_on` list. Tool nodes supply `tool` and `arguments`. Agent nodes supply `prompt` and receive dependency outputs. Human nodes supply a prompt.

Conditional routing uses `when: { "node": "dependency", "path": ["approved"], "equals": true }`. Tool arguments can contain `{ "$ref": { "node": "dependency", "path": ["content"] } }`. References must name declared dependencies. Skipped dependencies propagate skipping to descendants. Independent ready nodes execute in parallel and downstream nodes join after all their dependencies finish.

The current scheduler accepts DAGs with up to 100 nodes. Bounded loops, A2A nodes, richer join policies, configurable concurrency and graph editor UI remain pending.

## Persistence and side effects

Human pause inputs and transition back to running are committed together. Completed checkpoints survive restart and are reused. A node interrupted during execution becomes `uncertain`; it is not automatically replayed because external side effects may already have occurred. Reconciliation UI/API for uncertain work remains pending.

## Verification status

`tests/graph_probe.py` covers intended parallel execution, human pause and restart, resume without repeating completed reads, reuse of the single-agent engine, conditional skipping and uncertain-side-effect rejection. It currently fails before execution under xlang3 due to closure binding in nested comprehensions. `tests/nested_comprehension_probe.py` isolates the error. No CPython run, native fix, or graph workaround has been applied. User discussion is pending because generic runtime changes may overlap the separate xlang3 performance goal.

Graph endpoints and execution must not be claimed verified until this failure is resolved and the tests pass.
