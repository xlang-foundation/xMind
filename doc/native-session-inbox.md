# Durable session prompt inbox

The local browser, VS Code sidebar and interactive CLI let a user queue another
prompt while a session has an active run. The prompt is stored in the native
SQLite repository before the client reports it as saved. The active run
continues; the native backend admits the oldest queued prompt through the
ordinary run path when the session becomes idle. Clients can disconnect without
stranding accepted input. The backend advertises this ownership as
`session_inbox_dispatch` in `/v1/health`; browser and VS Code clients use that
capability to avoid duplicate client-side admission and attach to the run once
the native queue marks the item claimed. Older backends keep the client-side
fallback. The backend scans persisted inboxes at startup and resumes queued
items after restart. A concurrent admission conflict leaves the prompt queued
for a later retry.

A prompt posted to an idle session receives a one-second dispatch grace period
so the user can cancel it from the inbox. A prompt posted while the session has
an active root run wakes the dispatcher immediately; it is admitted after that
run settles. Startup recovery dispatches persisted prompts immediately.

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

`DELETE /v1/sessions/{id}/inbox/{item-id}` cancels an item only while it remains
queued. The canceled row is retained internally to reserve its stable ID, but
is omitted from inbox listings. If a concurrent run already claimed the item,
cancellation succeeds without changing its audit link. Repeating cancellation
is idempotent; reusing a canceled ID conflicts. Browser and VS Code show a
Cancel control for each pending item. All three clients can inspect and cancel
the same persisted inbox and attach to the durable run link after native
dispatch.

Run admission accepts `inbox_id` alongside the regular `/v1/runs` fields. In
one SQLite transaction, the repository verifies that the queued item belongs
to that session and that the admitted prompt exactly matches its content, then
creates the run and marks the item claimed with that run ID. A duplicate claim,
changed prompt, or competing run cannot consume the item. Claimed entries stay
in the inbox as an audit link to their run. The input is written to conversation
history once by ordinary run admission.

Release verification covers repository reopen/idempotency, exact prompt
claiming and duplicate-claim rejection, queued-item cancellation across two
repository connections, ID reservation and repeated cancellation, missing and
already-claimed cancellation, the real HTTP
run path during an active synthetic-provider turn, queue retention after a
rejected admission, subsequent dispatch after cancellation, and exactly one
user-history record. Browser contracts exercise the rendered Queue and Cancel
controls. VS Code host contracts verify queuing while active, cancellation and
dispatch of a persisted item when idle. The actual-server CLI contract queues a
prompt during a held inference stream, cancels the held run, observes automatic
admission of the queued prompt, and confirms exactly one history record and its
claimed run link. The native provider-profile contract also posts input to an
idle session and verifies that the backend runs it without a client polling or
draining the inbox, with the selected provider and model preserved in run and
history provenance. Provider inference in these contracts is synthetic.
