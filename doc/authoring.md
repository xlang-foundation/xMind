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
    default: false
    name: Security Reviewer
    description: Reviews assigned work for security issues.
    model_id: claude-sonnet
    mode: subagent
    hidden: false
    tools:
      - read_file
      - search_files
    instructions: |
      Review for security issues. Cite only evidence from your assigned branch.
```

`xmind admin ... import-agents FILE` stores at most 64 definitions through
xlang3-backed SQLite. A definition supplies a stable ID, a display name, a
description, bounded instructions, an optional model ID, and an optional tool
allowlist. Older definitions default the name to their ID and the description
to empty text. The optional `mode` is `all`, `primary`, or `subagent`; old
definitions default to `all`. `all` supports conversation and graph use,
`primary` is restricted to direct conversations, and `subagent` is restricted
to graph nodes and the bounded `delegate_tasks` tool. For delegation, a task
may set `agent_id` to a visible subagent ID; xMind pins that definition
revision, instructions, model and tool allowlist into the child. Its tools are
intersected with the parent agent's effective allowlist, so the child cannot
expand parent authority. The optional `hidden` flag defaults to `false` and
removes a definition from the browser and VS Code conversation picker; explicit
backend selection remains available. Graph import resolves the ID and
copies its revision, instructions, model, and tool binding into the immutable
graph plan. Updating an
agent and reimporting a graph creates a new graph revision; existing runs keep
their captured plan. A graph node using `agent_id` cannot override the
definition's model or instructions. A configured model ID must be enabled by
the backend before the graph can run. Agent IDs remain retired after removal.
A named definition does not grant tools or permissions: its optional allowlist
can only narrow the server-registered tools, workspace access and approval
policy. It can also add hard-deny rules that further narrow the selected
agent's tool access. Rules use the native tool name (or `*`) and a
workspace-relative glob. For example:

```yaml
permissions:
  - action: read_file
    resource: "**/.env*"
    effect: deny
  - action: run_process
    resource: "*"
    effect: deny
```

The backend checks these rules before dispatch and snapshots them with the
conversation and graph node. Only `deny` is supported today; `allow` and
interactive `ask` rules are rejected. A `run_process` rule is conservatively
matched against `*`, since a process can access files beyond its working
directory. Existing native effect approvals remain mandatory regardless of
agent rules.

Set `default: true` on one visible `all` or `primary` profile to make new
conversations use it automatically. The backend pins that agent revision into
the conversation when its first run is admitted. At most one profile may be
default; omitted values mean `false`, and a subagent or hidden profile cannot
be the default.

## Single-agent conversation selection

The xMind backend exposes the current agent catalogue at `GET /v1/agents` and
persists a revisioned selection per conversation at
`/v1/sessions/{id}/agent`. Selection snapshots contain public IDs, definition
revisions, model metadata, mode and visibility; instructions stay in the
backend and are pinned into the run at admission. Subagents cannot be selected
as conversation agents, and primary-only definitions cannot be admitted as
graph subagents. A conversation must be idle to change selection,
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

## Trusted xlang3 Python tools

The native AgentRunner can register trusted xlang3 callables from an explicit
startup YAML file:

```yaml
code_root: "D:/work/my-agent-code"
tools:
  - name: text.uppercase
    description: Convert text to uppercase.
    module: text_tools
    function: uppercase
    input_schema:
      type: object
      properties:
        text: {type: string}
      required: [text]
      additionalProperties: false
    execution: worker
```

`text_tools.py` exports `def uppercase(args): ...`; it receives a JSON object
and returns a JSON-serializable value. Pass the file with
`xmind view --python-tools-config FILE`, the local console's
`--python-tools-config FILE`, `xmind serve --python-tools-config FILE`, or
`Tools/agentflow.ps1 -Action Serve -PythonToolsConfig FILE`. The trusted
catalogue is stored through xlang3 SQLite and restored when the local backend
restarts. Changed configuration applies on the next backend launch. Registered
tools enter the regular model catalogue, and named-agent `tools` allowlists
continue to narrow which ones a conversation may use.

The default `execution: in_process` runs with the xMind server's privileges.
Trusted code is not a sandbox and can perform effects outside xMind's workspace
approval APIs. Set `execution: worker` to run the registered function in a
short-lived child xMind process over xlang3 shared-memory IPC. The backend
retains the model loop and approvals; cancellation terminates that child. This
process boundary does not restrict filesystem or network access, and it is not
an OS sandbox. Worker pooling and full effect mediation remain open work.

## Trusted xlang3 programming mode

For definitions that are easier to express as code, xMind can execute a trusted
xlang3 Python module with a native declaration API. The module exports
`configure(xmind)` and declares named agents and graphs:

```python
def configure(xmind):
    xmind.agent({
        "id": "security_reviewer",
        "model_id": "claude-sonnet",
        "instructions": "Review the selected change and cite concrete evidence.",
        "tools": ["read_file", "search_files"],
    })
    xmind.graph({
        "id": "review",
        "spec": {
            "nodes": [{
                "id": "inspect",
                "type": "agent",
                "agent_id": "security_reviewer",
                "prompt": "Review the selected change.",
            }],
        },
    })
    xmind.tool({
        "name": "review.checklist",
        "description": "Return a review checklist for supplied text.",
        "input_schema": {
            "type": "object",
            "properties": {"text": {"type": "string"}},
            "required": ["text"],
            "additionalProperties": False,
        },
        "module": "security_team",
        "function": "checklist",
    })

def checklist(args):
    return {"items": ["Check the evidence", "Check error handling"], "scope": args["text"]}
```

Run it with the backend stopped:

```powershell
xmind admin --db PROFILE_DB --modules XLANG3_MODULES --stdlib PYTHON_STDLIB `
  import-program CODE_ROOT security_team configure
```

`security_team.py` is imported from `CODE_ROOT`; the function name is explicit.
Pass the same profile database and xlang3 runtime roots that the backend uses.
The program receives native `agent(definition)`, `graph(definition)`, and
`tool(definition)` calls.
Definitions are converted to JSON and passed through the existing native
catalog validators and xlang3 SQLite stores. Importing never starts an agent or
graph. When both kinds are declared, agents are applied before graphs so
`agent_id` references resolve. The command reports catalog revisions and public
IDs, not instructions or keys. A program-defined tool has the same definition
as an entry in the trusted Python tools configuration and is later invoked
through the existing AgentRunner tool loop. Its module and function resolve
under `CODE_ROOT`. Declaring at least one agent, graph, or tool applies that
kind as a complete catalog and retires or replaces omitted prior definitions;
omitting all calls of a kind leaves its existing catalog untouched.

The module runs as trusted xlang3 code with the admin process's operating-system
privileges. This API does not sandbox Python or mediate arbitrary local effects.
It does not expose provider credentials or persistence handles to the module.
The same validators, model/tool allowlists, graph permissions and native runtime
remain authoritative after import, so YAML/Markdown and program-defined
catalogs converge on the same runtime definitions.

The native AgentRunner regression contract verifies the callable path
end-to-end: an xlang3 program declares the tool, the native importer stores it
in SQLite, the runtime reloads the registration, and AgentRunner returns the
actual xlang3 result to the synthetic model peer before persisting its
continuation. This proves the integration path, not live-provider behavior or
isolation of trusted code. See the
[contract evidence](evidence/agentflow-xlang-tool-runner-local-20261011.json).

## YAML subset and limits

YAML is parsed by the native yaml-cpp dependency and immediately converted to
the same JSON-shaped values consumed by existing validators. Inputs are limited
to 256 KiB, 8,192 nodes, 32 levels, 32 KiB per scalar, and 256 bytes per map
key. Duplicate keys, anchors, aliases, multiple documents, binary/timestamp
tags and integers outside JavaScript's exact-integer range are rejected.
Sequences, mappings, strings, booleans, nulls and JSON-compatible numbers are
supported. JSON input remains accepted.

## Still required for the agreed product

Workspace slash commands use bounded Markdown templates in `.agents/commands/`.
See [native workspace commands](native-commands.md) for the template fields,
argument rules, backend routes and remaining compatibility limits.

The current slice supports reusable named YAML agents in graph nodes, per-node
inline instructions, per-agent native tool allowlists, revisioned single-agent
conversation selection, trusted startup-registered xlang3 Python tools in the
AgentRunner catalogue, and trusted xlang3 programs that register named agent
and graph definitions through the native authoring API. Native effect mediation
for trusted code and durable agent-level worker execution remain incomplete.
Workspace Markdown skill discovery and selection are implemented,
while plugin-driven command providers and the wider skill lifecycle still have
documented gaps.
Those authoring styles must eventually produce the same validated native
definitions, tool registry and permission receipts; parser acceptance alone
does not establish runtime behavior. See the
[runtime architecture](runtime-product-design.md) for the complete target and
remaining integration work.
