# Agent and graph authoring

This page describes the authoring formats that the native xMind binary accepts
today. Declarative input is configuration data; importing a definition does not
execute an agent or graph.

## Graph catalogs

`xmind admin ... import-graphs FILE` imports a complete graph catalog while its
backend is stopped. The file may be JSON or bounded YAML. Each entry contains an
ID and a native graph specification:

```yaml
graphs:
  - id: review
    spec:
      nodes:
        - id: inspect
          type: agent
          prompt: Inspect the selected change and report findings.
          tools:
            - read_file
            - search_files
          instructions: |
            Act as a security reviewer. Cite only evidence from your assigned branch.
```

The native `GraphPlan` validator remains authoritative for node types,
dependencies, schemas and permissions. Agent nodes may include bounded
`instructions`, `model_id`, and an optional `tools` list. A tool list narrows
the server-authorized catalogue for that node; it cannot add tools, bypass
approval, or expand workspace permissions. Omitting `tools` keeps the normal
backend policy, while an empty list offers no tools. Bindings are checked
against the current native/MCP/A2A catalogue before inference. A catalog can hold at
most 16 graphs; IDs are stable and retired IDs cannot be reused. Import output
reports catalog and graph revisions plus node counts. `execution_available:
false` in the offline admin import result means import did not start a run. A
running server separately advertises live executability in `/v1/graphs` and
accepts registered graph runs through its shared execution service.

Graph nodes can instead reference a reusable named definition with `agent_id`:

```yaml
graphs:
  - id: review
    spec:
      nodes:
        - id: inspect
          type: agent
          agent_id: security_reviewer
          prompt: Inspect the selected change and report findings.
```

Import the definitions before importing graphs that reference them:

```yaml
agents:
  - id: security_reviewer
    model_id: claude-sonnet
    tools:
      - read_file
      - search_files
    instructions: |
      Review for security issues. Cite only evidence from your assigned branch.
```

`xmind admin ... import-agents FILE` stores at most 64 definitions through
xlang3-backed SQLite. A definition supplies bounded instructions, an optional
model ID, and an optional tool allowlist. Graph import resolves the ID and
copies its revision, instructions, model, and tool binding into the immutable
graph plan. Updating an
agent and reimporting a graph creates a new graph revision; existing runs keep
their captured plan. A graph node using `agent_id` cannot override the
definition's model or instructions. A configured model ID must be enabled by
the backend before the graph can run. Agent IDs remain retired after removal.
A named definition does not grant tools or permissions: its optional allowlist
can only narrow the server-registered tools, workspace access and approval
policy.

## Single-agent conversation selection

The xMind backend exposes the current agent catalogue at `GET /v1/agents` and
persists a revisioned selection per conversation at
`/v1/sessions/{id}/agent`. Selection snapshots contain public IDs, definition
revisions and model metadata; instructions stay in the backend and are pinned
into the run at admission. A conversation must be idle to change selection,
and clients send the observed session revision to prevent stale writes.

The VS Code sidebar and browser share the native-backed selector. In the
interactive console, `/agents` lists the catalogue, `/agent` shows the current
conversation selection, `/agent ID` selects a definition, and `/agent default`
clears it. Selecting an agent in an empty console starts a durable conversation
before applying the selection. The selected allowlist is snapshotted with the
conversation and enforced by the native model/tool loop; it grants no tools or
permissions.

## Agent instructions

`xmind admin ... import-instructions FILE` accepts a plain Markdown document,
JSON shaped as `{"instructions":"..."}`, or a YAML mapping with an
`instructions` field:

```yaml
instructions: |
  Follow the repository conventions.
  Explain the checks performed before proposing a file change.
```

The instruction text is a global supplement; it is not yet a named agent
definition and cannot grant tool permissions. Its revision and byte count are
printed, while the document body stays in xlang3-backed local SQLite. The
backend applies the usual prompt and tool policy when constructing model input.

## YAML subset and limits

YAML is parsed by the native yaml-cpp dependency and immediately converted to
the same JSON-shaped values consumed by existing validators. Inputs are limited
to 256 KiB, 8,192 nodes, 32 levels, 32 KiB per scalar, and 256 bytes per map
key. Duplicate keys, anchors, aliases, multiple documents, binary/timestamp
tags and integers outside JavaScript's exact-integer range are rejected.
Sequences, mappings, strings, booleans, nulls and JSON-compatible numbers are
supported. JSON input remains accepted.

## Still required for the agreed product

The current slice supports reusable named YAML agents in graph nodes, per-node
inline instructions, per-agent native tool allowlists, and revisioned
single-agent conversation selection. Arbitrary xlang3 `.py` callables and the
in-process programming API remain incomplete. A low-level trusted callable
bridge now embeds xlang3, imports a Python module from an explicit code root,
passes a JSON object to its function, and accepts only a JSON result. Its runtime
and values are created and destroyed on the invoking thread. This bridge is not
yet connected to agent YAML, the model tool catalogue, or the native tool
permission/approval APIs; do not treat it as registered AgentFlow functionality.
Trusted xlang3 code is in-process code and is not an OS sandbox. Workspace
Markdown skill discovery and selection are implemented, while the full
command/skill lifecycle still has documented gaps.
Those authoring styles must eventually produce the same validated native
definitions, tool registry and permission receipts; parser acceptance alone
does not establish runtime behavior. See the
[runtime architecture](runtime-product-design.md) for the complete target and
remaining integration work.
