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
          instructions: |
            Act as a security reviewer. Cite only evidence from your assigned branch.
```

The native `GraphPlan` validator remains authoritative for node types,
dependencies, schemas and permissions. Agent nodes may include their own
bounded `instructions` and `model_id`; the backend applies those instructions
to that node's LLM loop while retaining the server's registered tools,
permissions, repository instructions and global policy. A catalog can hold at
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
    instructions: |
      Review for security issues. Cite only evidence from your assigned branch.
```

`xmind admin ... import-agents FILE` stores at most 64 definitions through
xlang3-backed SQLite. A definition supplies bounded instructions and an
optional model ID. Graph import resolves the ID and copies its revision,
instructions and optional model into the immutable graph plan. Updating an
agent and reimporting a graph creates a new graph revision; existing runs keep
their captured plan. A graph node using `agent_id` cannot override the
definition's model or instructions. A configured model ID must be enabled by
the backend before the graph can run. Agent IDs remain retired after removal.
A named definition does not add tools or permissions: the same server-
registered tools, workspace access and approval policy still apply.

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

The current slice supports reusable named YAML agents in graph nodes and
per-node inline instructions. Single-agent session selection of a named
definition, per-agent tool bindings, filesystem skill catalogs, arbitrary
xlang3 `.py` callables, and the in-process programming API remain incomplete.
Those authoring styles must eventually produce the same validated native
definitions, tool registry and permission receipts; parser acceptance alone
does not establish runtime behavior. See the
[runtime architecture](runtime-product-design.md) for the complete target and
remaining integration work.
