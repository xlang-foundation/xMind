# Local session archives

xMind can export and import a conversation through the local native backend.
The archive is a UTF-8 JSON object with `format: "xmind.session"` and
`version: 1`. It contains the session title and ordered settled messages as
`{role, data}` entries. Message IDs and sequence numbers are omitted because
they are local to each SQLite database.

Use the console client with the same local backend credentials used by other
commands:

```text
xmind_cli PORT export-session SESSION_ID FILE
xmind_cli PORT import-session FILE
```

The equivalent native API is `GET /v1/sessions/{id}/export` and
`POST /v1/sessions/import` with `{ "archive": ARCHIVE }`. Import creates a
fresh session ID and returns it. The source ID, fork ancestry, workspace,
provider selection, agent selection, skills and pending inbox are not copied.
Imported messages receive new sequence numbers in the destination database.

Exports are rejected while a run for the session is queued, running or paused.
Archives are limited to 1,000 messages, 256 KiB per message and 4 MiB total;
imports accept only `user`, `assistant` and `tool` roles. CLI export refuses to
overwrite an existing path. An archive includes the conversation content,
which may contain user-provided sensitive information; provider credentials
are not part of the archive format.
