# Local session archives

xMind can export and import a conversation through the local native backend.
The archive is a UTF-8 JSON object with `format: "xmind.session"` and
`version: 2`. It contains the session title, the selected agent snapshot (or
`null`), and ordered settled messages as `{role, data}` entries. The agent
snapshot preserves its ID, revision, model ID, instructions and optional tool
allowlist; it contains no provider credential. Version 1 archives remain
importable without an agent snapshot. Message IDs and sequence numbers are
omitted because they are local to each SQLite database.

Use the console client with the same local backend credentials used by other
commands:

```text
xmind_cli PORT export-session SESSION_ID FILE
xmind_cli PORT import-session FILE
```

The browser session selector also has Export and Import controls. Export
downloads the selected idle conversation as a JSON archive; Import asks you to
choose an archive, creates a new local conversation and selects it.

The equivalent native API is `GET /v1/sessions/{id}/export` and
`POST /v1/sessions/import` with `{ "archive": ARCHIVE }`. Import creates a
fresh session ID and returns it. The source ID, fork ancestry, workspace,
provider profile, workspace-bound skills and pending inbox are not copied.
Imported messages receive new sequence numbers in the destination database.

Exports are rejected while a run for the session is queued, running or paused.
Archives are limited to 1,000 messages, 256 KiB per message and 4 MiB total;
imports accept only `user`, `assistant` and `tool` roles. CLI export refuses to
overwrite an existing path. An archive includes the conversation content,
which may contain user-provided sensitive information, including agent
instructions; provider credentials are not part of the archive format.
