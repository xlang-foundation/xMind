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
```

The native `GraphPlan` validator remains authoritative for node types,
dependencies, schemas and permissions. A catalog can hold at most 16 graphs;
IDs are stable and retired IDs cannot be reused. Import output reports catalog
and graph revisions plus node counts. `execution_available: false` means the
admin command only registered definitions; it did not start a run. Running a
saved catalog through a reusable public graph API is still in progress.

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

The current slice does not yet define named YAML agents, tool bindings, skill
catalogs, arbitrary xlang3 `.py` callables, or the in-process programming API.
Those authoring styles must eventually produce the same validated native
definitions, tool registry and permission receipts; parser acceptance alone
does not establish runtime behavior. See the [runtime architecture](runtime-product-design.md)
for the complete target and remaining integration work.
