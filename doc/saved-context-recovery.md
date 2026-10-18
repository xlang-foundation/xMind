# Saved conversation context after a backend change

## Current profiled execution across providers

Active profiled runs use a separate native ownership path to prepare original
conversation groups for a selected provider. It checks the admitted target,
actual producing model-call receipt, captured profile/model, workspace and exact
complete-group bytes. The gateway validates each source receipt before changing
the request copy. The typed publication rechecks the source in a transaction.
Original messages, private receipts and supplied metrics stay unchanged in SQLite.
Responses checkpoints remain private to their model and authority; other
providers receive checked originals rather than a foreign checkpoint.

DeepSeek Chat Completions has an additional restriction: its tool mode needs its
own prior reasoning, and this wire does not support inserting foreign tool-call
turns. See the official [thinking-mode](https://api-docs.deepseek.com/guides/thinking_mode/)
and [tool-call](https://api-docs.deepseek.com/guides/tool_calls/) documentation,
checked October 18, 2026. Checked foreign assistant/tool groups are therefore
represented as labelled, untrusted conversation data in system context messages.
Each record retains the public assistant content/refusal, exact call arguments
and correlated observed tool-result bytes. Original user objectives remain user
messages; DeepSeek's own native assistant/tool continuations retain their supplied
reasoning. The conversion neither invents reasoning nor disables thinking.

Unproved and incomplete source groups still fail closed. The ordinary strict
continuation API remains separate. Idle maintenance, unprofiled recovery,
rendered clients and installed-package acceptance have their own remaining gates;
see [current validation status](VALIDATION_STATUS.md). Earlier receipts below
describe their pinned source checkpoints, not the full current implementation.

## Earlier Responses-only active-run recovery

The initial implementation rebuilt an outgoing Responses-context request when a proved original
receipt belongs to a different authority under the same captured provider
identity, registered context policy and strategy. A selected model may also
change within the same captured Responses provider profile, profile revision,
route, provider and wire. Both models use their own context binding and
checkpoint; a checkpoint from the other model is never relabeled. The producing budget must
also belong to the active execution workspace. Full original messages, tool
turns, metrics and opaque receipts stay unchanged in SQLite. Old authority
heads and checkpoints are not relabeled.

The backend loads bounded original groups through its native context journal.
Each source must match its immutable group digest, producing inference member,
completed model-call receipt and committed conversation event. The model gateway
validates the native receipt against canonical content, refusal and tool-call
fields. It then omits the old provider continuation from the request copy while
retaining those canonical fields and their correlated tool results. Current
trusted instructions and tools are supplied separately. The ordinary strict
continuation-loading API continues to reject a different authority.

`record_context_history_replay` is a typed repository operation. Before recording
`context.history_reprojected`, it rechecks the active root/owner, current source
snapshot, original provenance and producing workspace in a transaction through
embedded xlang3. Generic event append cannot manufacture this lifecycle event.
Its observation contains counts and bindings, without provider keys or private
continuation data. The shared renderer explains that context was refreshed and
recorded history was preserved.

This path applies to active run preparation and its compaction input. It does
not repair a failed profile handoff or migrate package/profile formats. Foreign
provider profiles/routes/revisions, unprofiled model identities, unregistered policies, unproved originals, different
producing workspaces and histories beyond the native bounds remain rejected.
Idle maintenance retains its strict loading path. The focused Release gate
passed **7/7 native contracts in 22.48 seconds**. The independent synthetic
provider verifies two actual native read loops across a persistence reopen and
changed instructions, followed by a new current-authority compaction. It checks
that prior native item IDs are absent from the new request while canonical tool
results and both user objectives remain. Queued-owner/generic-event forgery,
foreign identity, stale snapshot and different-workspace cases are rejected.
The shared client suites passed **393 extension and 136 browser checks**.
These are native protocol/DOM contracts. [Source and validation record](evidence/context-authority-replay-20261018.json).

A subsequent live saved-OpenAI check at `097ee32` completed three runs in the
same conversation across same-package edit-policy and process-configuration
handoffs. It read a phrase, remembered it while creating an approved file, and
used xlang3 to assert the exact file bytes with exit code zero. The command's
actual assertion was inspected; public history prefixes stayed unchanged at
4, 8 and 12 messages. No private database mutation or CPython was used. The
current browser HTTP adapter exposes the same conversation and saved-key model
discovery. External rendered interaction remains unverified because Windows
denied browser activation; the installed VS Code runtime remains `d7adf96`.
No recovery of the prior failed profile handoff, older-binary migration or
same-session provider/model change is established by those `097ee32` checks.
[Live coding and browser HTTP evidence](evidence/context-authority-replay-live-20261018.json).

At `758e38f`, another live conversation completed
`gpt-6.1-sol → gpt-6-sol → gpt-6.1-sol` on the same saved OpenAI Responses
profile. It read a phrase, used that conversation to create an approved file,
then asserted the file bytes through xlang3. All three runs completed and the
previous public history prefixes remained unchanged. The current browser HTTP
adapter exposes the same conversation and model catalogue. Starting that view
while a run was active failed at provider-configuration import; it succeeded
after the run completed. Active-run reconnect therefore remains unfinished.
[Live model-switch acceptance and limits](evidence/context-model-switch-live-20261018.json).

## Earlier observed limitation and installed-client evidence

The 2026-10-18 runtime update reproduced this limitation: run
`c74d62c1f86ab26760c1bd34225ca76d` failed `context_unavailable` before any
model call after the tool/runtime authority changed. A fresh conversation
completed the coding checks, while the prior history remained stored.
At that checkpoint automatic continuation across the authority change was unfinished.
A separate profile handoff also remained unresolved; fresh-profile acceptance
does not establish recovery of that handoff.
[Current run and package evidence](evidence/native-wheel-live-coding-20261018.json).

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
invent model responses or token usage. These messages are now included in the
installed 0.1.3 VS Code client. The client was reloaded with the same native
backend and restored the completed creation run before a fresh conversation was
opened. This does not repair or relabel the old conversation's context receipts.
See [installed client update](evidence/vscode-sidebar-recovery-installed.json).

Validation: 215 extension contracts and 39 browser contracts passed, including
failure-message retention across transcript refresh, with 43 input files and 11
freshly generated browser assets frozen before/after the suites. These are
isolated adapter/DOM tests, not native inference or rendered writing acceptance.
See [recorded evidence](evidence/saved-context-recovery.json).
