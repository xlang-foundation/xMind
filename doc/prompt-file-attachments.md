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

The VS Code extension can attach files selected from folders registered in its current xMind workspace. The CLI and browser do not yet expose attachment controls, and graph-run submission does not yet accept attachments. Those gaps remain in the OpenCode 2 parity inventory.
