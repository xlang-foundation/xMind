# Outbound A2A artifact fetch checkpoint

The native task-inspection route accepts the local boolean `include_artifacts`
and maps it to A2A `GetTask`'s `includeArtifacts`. Ordinary inspection omits
artifacts from the request. The CLI exposes `a2a-inspect OPERATION_ID
--include-artifacts`; the browser and VS Code operation renderer expose a
separate **Fetch remote artifacts** control. The returned task, including
artifact parts, remains a bounded read-only JSON observation persisted through
xlang3 SQLite. The UI renders it as text; xMind does not follow peer URLs or
write artifact bytes into a workspace.

## Local verification

- VS Code extension/host/renderer suite: **318/318 passed**.
- Browser controller and integration suite: **97/97 passed**.
- The renderer test verifies that artifact fetching is a separate,
  explicit control, emits `includeArtifacts: true`, and renders a script-shaped
  artifact as escaped text. Its result is included in the current VS Code test
  suite after the renderer test addition.
- `node tools/audit-opencode.mjs`: **141 pinned operations, 32 native source
  candidates, 109 unmapped, zero verified operation-level parity**. The source
  hashes were regenerated against current native source.
- Visual Studio 18 Release build of `xmind`, `agentflow_a2a_client_contract`
  and `agentflow_a2a_agent_registry_contract`: passed.
- Focused A2A client, official-SDK and registry CTests: **3/3 passed**. The
  native client contract asserts that explicit `includeArtifacts: true` reaches
  the JSON-RPC peer. The independent official A2A JS SDK 1.3.0 peer contract
  emits an actual text artifact, and both its SDK client and xMind's native
  client verify the returned artifact body. The HTTP registry contract posts
  `include_artifacts: true`, verifies the returned artifact text, and confirms
  that the same observed response is persisted in xlang3 SQLite.
- `git diff --check`: passed.

The peers are local fixtures, not live external agents. The JS fixtures exercise
UI request propagation and escaped rendering, not installed-client behavior.
Packaged-client acceptance and the broader A2A parity gate remain open.
