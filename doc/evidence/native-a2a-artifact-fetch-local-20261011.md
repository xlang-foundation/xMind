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

- VS Code extension/host/renderer suite: **316/316 passed**.
- Browser controller and integration suite: **97/97 passed**.
- The renderer test verifies that artifact fetching is a separate,
  explicit control, emits `includeArtifacts: true`, and renders a script-shaped
  artifact as escaped text. Its result is included in the current VS Code test
  suite after the renderer test addition.
- `node tools/audit-opencode.mjs`: **141 pinned operations, 32 native source
  candidates, 109 unmapped, zero verified operation-level parity**. The source
  hashes were regenerated against current native source.
- `git diff --check`: passed.

The native C++ build and CTest could not run in this shell: CMake, MSBuild and
the Visual C++ compiler are unavailable. The native route and updated A2A
client contract therefore remain uncompiled/unexecuted here. The JS fixtures
exercise client request propagation and escaped rendering, not an independent
or live A2A peer. Official SDK/peer conformance, packaged-client acceptance and
the broader A2A parity gate remain open.
