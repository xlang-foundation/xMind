# Named-agent placement

Primary agent definitions can select the backend's native execution owner:

```yaml
agents:
  - id: repository.reader
    mode: primary
    placement: process
    instructions: Read the requested source and report the observed evidence.
    tools: [read_file, list_files, search_files, find_files, glob_files]
```

`process` requires a configured native worker executor. Admission fails before
creating a run when that owner is absent. The current production worker factory
supports ordinary read roots and explicit empty tool lists. It rejects unsupported
edit, process-tool, remote-tool, planning, delegation and context capabilities.
The standalone unified server enables this factory with
`xmind serve --agent-execution process-read`, alongside the normal database,
runtime, authentication, workspace and provider configuration. This mode offers
the five native read tools above and disables agent effect/remote capabilities.
It requires `--workspace` and rejects approved edits and explicit effect-tool
configuration. The default `--agent-execution thread` retains normal execution.

Managed local-profile launch and adoption reject `process-read` until the owner
contract can capture that mode; this option currently applies to standalone
server startup. The paired `xmind.exe` is retained as the worker executable.

`placement: thread` uses the backend's native runner even when a worker executor
is configured. Omitting placement preserves that generation's backend default.
Process placement currently requires `mode: primary`; graph and subagent placement
remain unfinished.

Placement changes increment the definition revision. Selecting an agent pins its
placement with the rest of the session definition. Later catalog edits preserve
that pinned choice. New root admission copies it into the dispatch job and prompt
receipt, so a queued job does not select placement from a later catalog revision.

Public metadata includes explicit placement. The shared browser/extension client
validates it, checks selection/catalog agreement, and labels the agent option as
“separate process” or “backend thread.” Installed and live-rendered placement
acceptance remains open.

The [standalone entry contract](evidence/agent-worker-server-entry-focused-20261020.json)
qualifies actual authenticated HTTP admission, worker-backed native reads,
canonical history and events through the public client on a controlled provider.

The [native qualification](evidence/agent-placement-focused-20261020.json) covers
actual explicit process and thread roots, missing-owner rejection and persisted
session pins. [Client qualification](evidence/agent-placement-client-validation-20261020.json)
covers the metadata validator and selector DOM through separate Node suites.
Explicit queued placement, recovery and low-level runner enforcement need further
qualification; no new full-suite or installed acceptance is claimed.
