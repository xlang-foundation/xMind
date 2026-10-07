# CLI graph observation

`xmind_cli PORT graph-watch ROOT [AFTER]` extends the existing native console observer to graph roots. It emits actual persisted root and child events as newline-delimited JSON, preserving event sequences and original run identities. Use the last emitted sequence as `AFTER` to reconnect. It neither grants effects nor requests cancellation when the console closes. Single-run `watch` retains its existing behavior.

The observer verifies the graph root and immutable session identity. Each event batch is followed by a child-ownership read, so an event for a child admitted between requests can be validated. Only the selected root and children with matching parent/session identities can be emitted. A child ID cannot be selected as the root. Terminal status is followed by a final event drain. Exit status is 0 for completion, 2 for cancellation and 1 for failure or observation errors. Tool output remains escaped JSON rather than executable terminal control text.

The graph service HTTP/CLI contract now covers closing an observer while the native root remains paused, reconnecting from its cursor after actual human input/file execution, exact remaining event replay with child identities, final event drain, a caught-up cursor, rejection of a child as root and cancellation exit status. Existing actual file effects and synthetic provider fixture scopes remain unchanged.

## Validation status

Source and regression cases are prepared. The Node contract syntax check and source whitespace check pass. Native compilation/execution is **not yet verified**: the guarded local build returned exit status 3 because live sibling xlang3 benchmark processes were detected on two checks. No benchmark was stopped and no old binary was used to claim the new command works. The unchanged Windows CI graph-service contract will compile and exercise these new cases alongside the complete native suite. Hosted acceptance is pending.

The existing webpage remains on its previously verified native snapshot; this CLI source checkpoint does not replace its backend or affect its provider key/session state.
