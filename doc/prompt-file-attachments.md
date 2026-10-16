# Prompt file attachments

`POST /v1/runs` accepts an optional `files` array for a single-agent run:

```json
{
  "session_id": "session-id",
  "prompt": "Review this file",
  "files": [
    {"path": "src/app.cpp", "folder_id": "registered-folder-id", "name": "app.cpp"}
  ],
  "expected_workspace_id": "registered-workspace-id",
  "expected_workspace_authority_id": "current-workspace-authority",
  "expected_workspace_revision": 7
}
```

Clients send workspace-relative paths, optional display metadata, the current workspace authority and the observed membership revision. They never send file bytes. The native backend requires both authority values to still match, resolves each path against the immutable registered folder membership at that revision, reads UTF-8 content through the workspace file boundary, then persists the exact snapshots with the user input before scheduling model execution. A stale membership revision rejects admission. The model receives each captured file as explicitly untrusted text; subsequent filesystem edits do not change the admitted snapshot.

The request supports at most 32 files, 512 KiB of aggregate file content and a 2 MiB serialized persisted prompt. A file uses the existing workspace reader's 1 MiB per-file bound, so large single files are rejected by the aggregate cap. Traversal, unregistered folders, paths outside membership, non-text files and symlink escapes are rejected before a run or history entry is created. File attachments cannot be submitted through the durable queued-input route yet.

The VS Code extension and browser can attach files from folders registered in the current xMind workspace. The CLI accepts multiple `--file PATH` arguments for both single-agent and graph runs; paths are relative to the primary folder by default. Use `--folder-id ID` before a `--file` to select another registered folder, for example:

```powershell
xmind run SESSION "Review these files" --file src/main.cpp --folder-id docs-folder --file README.md
xmind graph-run SESSION review GRAPH_REV "Review these files" --file src/main.cpp --folder-id docs-folder --file README.md
```

All clients send workspace-relative references only. The native backend verifies workspace identity, authority and membership revision, reads bounded UTF-8 content through the registered folder boundary, and persists immutable snapshots before dispatch. Graph snapshots are copied to graph agent nodes as untrusted data; graph tool nodes remain explicit registered operations. Durable queued-input attachments remain unsupported and are reported in the OpenCode 2 parity inventory.
