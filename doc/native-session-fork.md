# Native session fork

The native backend now forks a persisted conversation through the xlang3-backed
SQLite repository. `POST /v1/sessions/{id}/fork` accepts the required JSON body
`{"before":"msg_<sequence>"}` or `{"before":null}`. A `before` boundary is
exclusive; `null` copies through the newest session message. Session history
returns stable message IDs as `msg_<seq>` alongside its numeric sequence.

Fork creation is one transaction: it creates a new session with a numbered
`(fork #N)` title, stores parent/boundary lineage, copies settled transcript
messages before/through the boundary, and snapshots the selected agent and all
workspace-scoped skill selections. The source session is untouched. A boundary
must identify a visible message in the selected source session; malformed IDs
return 400 and missing/foreign message IDs return 404. An empty source cannot
be forked. Active-run messages are excluded from copied history.

The native CLI exposes the same operation as:

```text
xmind_cli fork-session SESSION [BEFORE_MESSAGE_ID]
```

Focused native contracts exercise full and exclusive-boundary forks, message
ID mapping, source isolation, wrong/malformed boundaries, agent/skill snapshot
copying, HTTP and CLI access, and xlang3 SQLite restart recovery. Browser and
VS Code now expose a per-message “Fork before this message” control when the
backend advertises native fork support. Both clients bind the request to the
selected saved conversation, reject malformed/foreign boundaries and active
runs, validate the native receipt, and switch to the new child session. The
browser and VS Code controller/renderer suites cover those paths. The operation
is recorded as a native source candidate, not verified OpenCode parity;
OpenCode's wider session metadata, typed message parts, and provider usage/cost
behavior remain outside this slice.
See the pinned [OpenCode parity inventory](PARITY.md).
