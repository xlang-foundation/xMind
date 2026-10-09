# Saved conversation context after a backend change

A conversation created by an older read-only backend can remain readable after
upgrading to a backend with file creation/edit proposals enabled. Its original
provider continuation receipts retain their producing authority binding. The
native backend rejects reuse when that binding differs from the current
configuration; it must not relabel the old receipts or silently drop history.

In the observed VS Code incident, run `ceeb49df9ff22fefb5b999895edd8147`
failed with `context_unavailable` before any provider execution event. A
query-only diagnostic executed by xlang3 read the selected run and context
metadata from the installed backend's SQLite database. Its old inference
receipts and newly selected context head had different authority identities.
No provider keys, message contents or stored opaque continuation data were
printed, and the diagnostic issued no database mutations.

Recovery is to use **New conversation** in the xMind sidebar and send the coding
request there. The earlier session and its history remain available in the
history chooser. The observed fresh conversation showed **Ready**, root
`D:\CantorAI2026\TestProj`, and **File changes require approval**. A creation
request was submitted there and produced a real native `create_file` proposal.
Approval through the installed sidebar created `hello.py` with
`print("Hello from xMind")` followed by a newline. The operation and run completed;
the final response and provider token metrics were rendered. Independent disk
inspection matched the proposed bytes. This establishes creation through the
installed 0.1.1 preview, not the newer regex candidate or broader editor parity.
See [installed writing evidence](evidence/vscode-file-creation-live.json).

The shared renderer now explains `context_unavailable` and
`context_binding_changed`, directs users to a fresh conversation, and states that
recorded history is preserved. It does not interpolate exception messages or
invent model responses or token usage. The installed renderer has not yet been
updated; these messages are in source for the next client package.

Validation: 215 extension contracts and 39 browser contracts passed, including
failure-message retention across transcript refresh, with 43 input files and 11
freshly generated browser assets frozen before/after the suites. These are
isolated adapter/DOM tests, not native inference or rendered writing acceptance.
See [recorded evidence](evidence/saved-context-recovery.json).
