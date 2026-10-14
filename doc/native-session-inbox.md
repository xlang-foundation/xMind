# Durable session prompt inbox

The local browser and VS Code sidebar let a user queue another prompt while a
session has an active run. The prompt is stored in the native SQLite repository
before the UI reports it as saved. The active run continues; when the session
becomes idle, the client admits the oldest queued prompt through the ordinary
native run path. A concurrent admission conflict leaves the prompt queued for a
later retry.

`GET /v1/sessions/{id}/inbox` returns `{items:[...]}` in enqueue order. Each item
contains its stable `id`, `session_id`, exact `content`, `state` (`queued` or
`claimed`), `created_ms`, and `run_id` when claimed. The result is bounded to
128 records.

`POST /v1/sessions/{id}/inbox` accepts `{content}` and an optional caller-chosen
`id`, and returns the stored item with HTTP 202. Content must contain 1 to
1,048,576 UTF-8 bytes and no NUL. Per session, at most 32 pending prompts and
4 MiB of pending content are accepted. Repeating an ID with the same session
and content returns the original record; reusing it with different content or
another session is a conflict.

Run admission accepts `inbox_id` alongside the regular `/v1/runs` fields. In
one SQLite transaction, the repository verifies that the queued item belongs
to that session and that the admitted prompt exactly matches its content, then
creates the run and marks the item claimed with that run ID. A duplicate claim,
changed prompt, or competing run cannot consume the item. Claimed entries stay
in the inbox as an audit link to their run. The input is written to conversation
history once by ordinary run admission.

Release verification covers repository reopen/idempotency, exact prompt
claiming and duplicate-claim rejection, the real HTTP run path during an active
synthetic-provider turn, queue retention after a rejected admission, subsequent
dispatch after cancellation, and exactly one user-history record. The browser
classic-script contract checks the rendered Queue button, prompt submission and
visible pending item. Provider inference in these contracts is synthetic.
