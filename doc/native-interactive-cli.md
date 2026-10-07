# Interactive native CLI sessions

The C++ CLI now has source for `xmind_cli PORT chat [SESSION [MODEL]]`. It uses the same authenticated xMind Server, persistent conversations and dynamic native agent/tool loop as the editor and browser. It does not execute a separate agent or access SQLite directly.

With a configured backend and `XMIND_AUTH_TOKEN` privately set, enter requests at `xMind >`. A new conversation is created only after the first non-empty request; opening chat and immediately leaving does not create placeholder history. A supplied session is validated and reused. A supplied model is sent through normal native model-selection validation; otherwise the server's configured default is used. Enter `/exit` between turns to leave.

Each admitted run is observed to its actual terminal state. Standard output contains escaped NDJSON session/run descriptors and original durable events, including tool results and supplied usage. Prompts and connection errors use standard error. No token usage, assistant text or tool effects are synthesized by this client. Reconnecting to an existing session adds only the requested new turn; completed work is not replayed.

Resuming an existing session emits a `history` record containing the actual saved
conversation before accepting input. Entering `/exit` immediately displays that
history without admitting a run. A failed turn emits its recorded failure and a
`turn_finished` record with exit status 1. The user can submit another explicit
request in the same session; the CLI does not automatically retry the failed
request. Process exit status reflects the last observed turn (or zero if no turn
was admitted).

Closing or interrupting the CLI leaves backend ownership unchanged. `/exit` is read between completed turns, not during the blocking observer. Explicit cancellation and effect approvals currently use the existing `cancel`, `operations`, `operation` and `decide` commands from another console or an authenticated view. Integrated concurrent input, terminal approval/diff controls, rich TUI rendering, attachment/context controls and full OpenCode CLI parity remain required.

## Verification status

The actual-native HTTP/CLI contract is extended to exercise an empty launch, two model/tool turns in one new session, reconnection for a third turn, read-only history on reconnect, failed-turn exit status, a later explicit request after failure, and persisted history after server restart. Its provider replies are labelled synthetic; file reads and backend/CLI/database execution are real when the contract runs.

This source has **not yet passed compiled validation**. Node contract syntax and source whitespace checks pass. The guarded native build deferred with exit status 3 after confirming active sibling xlang3 benchmark processes. No old binary was used to claim the new command works. The isolated Windows pipeline runs the extended contract with the complete native suite; current-source execution is pending. This checkpoint does not replace any running preview.
