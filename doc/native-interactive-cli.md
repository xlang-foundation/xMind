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

Between turns, `/models` displays the backend-enabled model catalogue and
`/model ID` selects an advertised ID for subsequent requests in this CLI client.
`/model` returns to the server default. Selection does not rewrite shared provider
settings. `/history` reads the current durable conversation, and `/help` describes
the commands. These commands do not create a session or invoke inference.
Unavailable IDs are rejected without changing selection. Prefix a literal slash
request with a second slash (`//`); unknown commands are not sent to the model.
The optional initial model argument is checked against the backend catalogue
before displaying history or accepting requests. Its regression cases require an
unavailable ID to leave runs, history and provider request counts unchanged, and
an enabled ID to retain normal read-only session inspection. These newer cases
remain pending compiled validation.

The catalogue is `/v1/models`, the executor's currently enabled models. It is not
the provider account discovery endpoint; enabling additional providers/models is
a separate backend configuration action. Integrated console settings and live
model-selection acceptance remain pending.

`/provider-models` now requests account discovery through the native backend's
saved encrypted credential and current provider revision. The CLI sends only
`expected_revision`, not a provider key. It emits a `provider_models` record and
does not change shared settings or admit a model run. The provider setup contract
adds an interactive discovery check with the actual CLI and synthetic provider,
including omission of fixture keys from output. This source and its compiled
contract are pending validation.

During chat observation, pending effect proposals are read from the backend and
shown as escaped NDJSON `operation_review` records, including the exact argument
bytes, file snapshots or command specification, ownership and expiry. Enter
`/allow ID` or `/deny ID` using the displayed ID, or `/cancel` to request run
cancellation. Decisions contain no client-supplied plan or authority; the backend
revalidates the operation. Rejected/stale decisions cause state to be reread.
No approval is automatic. EOF or `/exit` at the approval prompt detaches with an
error status and sends no decision or cancellation. Backend execution continues.

Closing or interrupting the CLI leaves backend ownership unchanged. Input is
currently read between turns and at an approval prompt, not concurrently during
model streaming. Existing `cancel`, `operations`, `operation` and `decide`
commands remain available from another console/view. Concurrent input, rich
terminal diff/TUI controls, attachment/context controls and full OpenCode CLI
parity remain required. The edit HTTP contract now drives this approval path
through the actual CLI for both allow and deny, with synthetic inference and real
native file effects when compiled validation runs; execution is still pending.
Additional cases reject an unrelated operation ID before accepting the exact
displayed ID, and detach while leaving the file unchanged and the backend
operation awaiting approval. The detached fixture is cancelled separately by
the authenticated test controller. These are pending contract cases, not observed
product acceptance results.

## Verification status

The subsequent checkpoint `d3a395d89a1ecf93947728338a65822c441464bd` also
passed its [hosted gate](https://github.com/xlang-foundation/xMind/actions/runs/37702741919):
52 native and 66 extension contracts. This verifies initial-model prevalidation
and `/provider-models` saved-key discovery in the actual CLI, in addition to the
earlier controls/approval cases. [Exact-source scope](evidence/native-cli-discovery-setup-hosted-provenance.json),
[projected results](evidence/native-cli-discovery-setup-hosted-summary.log).
The source-prepared notes above/below retain their historical introduction
status; this exact hosted result supersedes pending status for those features.
New reasoning and Responses enrollment/routing changes remain separately pending.

Update: `d0a70fef888c3724c2490bcb2cf8ef38d60f08c0` passed its
[hosted gate](https://github.com/xlang-foundation/xMind/actions/runs/37701625318)
with 52 native and 66 extension contracts. This extends the initial chat result
to resumed-history output, failed-turn recovery, slash/model/history commands
and interactive approval/denial, unrelated-ID rejection and detachment without
a decision. [Exact revision/results](evidence/native-cli-approval-diagnostics-hosted-provenance.json).
Initial model prevalidation and `/provider-models` were added after this tested
revision and still await compiled validation. Public launcher and installed
checks retain their separately stated local scope. The historical checkpoint
notes below describe when each portion was introduced.

The actual-native HTTP/CLI contract is extended to exercise an empty launch, two model/tool turns in one new session, reconnection for a third turn, read-only history on reconnect, failed-turn exit status, a later explicit request after failure, and persisted history after server restart. Its provider replies are labelled synthetic; file reads and backend/CLI/database execution are real when the contract runs.

The initial interactive chat checkpoint `51724ab8fdda3b9349b18f110a1256ec4f1398eb`
passed compiled hosted validation: 52 native and 63 extension contracts. Its
native HTTP/CLI case exercised empty launch, two shared-session turns,
reconnection and durable history after restart. [Run](https://github.com/xlang-foundation/xMind/actions/runs/37700262597),
[projected contract log](evidence/native-interactive-chat-hosted-contract-summary.log),
[exact-source provenance](evidence/native-interactive-chat-hosted-provenance.json).

Later resumed-history output, failure recovery cases, slash/model commands and
interactive approval controls described above are newer source and **remain
pending compiled validation**. Node syntax/whitespace checks pass. The guarded
local build still defers while sibling benchmarks are live. No old binary proves
these newer features. No running preview has been replaced by this checkpoint.

The exact hosted CLI bundle for revision `51724ab` was downloaded and installed
in this machine's ignored `.agentflow/ci/installed/51724ab8fdda3b9349b18f110a1256ec4f1398eb/`
folder after archive-path and provenance checks. Its health request and empty
interactive chat passed against the existing browser native backend. Chat exited
zero, emitted no fabricated records and left the session count unchanged.
[Local installed-binary evidence](evidence/native-interactive-chat-local-installed.json)
records the binary hash and scope. This local check submitted no provider request
and does not validate the newer CLI features or live coding. Other previews and
their backend processes were left running.

`Tools/agentflow.ps1 -Action Chat -Port PORT` is the public console entry point.
It accepts `-Session ID` and optional `-Model ID` for an existing session, and
`-BinaryDirectory DIR` for an installed native distribution. It forwards normal
native arguments and inherits the existing private authentication environment;
it does not copy keys or access SQLite. This public launcher also passed the
empty-chat check above. `-Action Serve -ModelWire responses` now forwards the
explicit native wire option; that forwarding was checked against the server
argument contract, not a live Responses request.

Interactive chat now has `/sessions`, `/session ID` and `/new` in source.
Listing and resuming use the selected server's actual catalogue/history. Invalid
or missing IDs preserve the current selection. `/new` clears the console's
selection and displays empty history, creating no database session until the
next real request. These commands preserve model selection and the last actual
turn's exit status. The native provider CLI contract now navigates a labelled
persisted user-message fixture, checks exact history and verifies no runs,
duplicate sessions or provider configuration changes. Syntax checks passed;
compiled execution of these additions is pending because local compilation was
deferred by active sibling xlang3 benchmarks. This does not claim a complete TUI
or full OpenCode CLI parity.

New console conversations now use the first request as their title, bounded to
80 UTF-8 bytes without splitting a character. Leading whitespace is omitted
from the title, and control bytes are rendered as spaces. The original submitted
prompt remains unchanged. Whitespace-only console input creates no conversation
or run. Existing saved titles are preserved. This source change awaits native
compilation; the installed browser preview still runs the earlier verified CLI
and retains its original conversation titles.

Native conversation renaming is now implemented in source through the C++
repository/persistence service and `POST /v1/sessions/ID/title`. Clients supply
`title` and `expected_title`; a mismatched current title returns conflict before
mutation. SQLite I/O uses embedded xlang3 and changes only the selected session's
title. The CLI exposes `/title NAME` for the selected conversation and
`rename-session ID TITLE EXPECTED_TITLE` for one-shot use. Invalid titles and
conflicts leave the selection intact. The shared backend client has the same
typed request, and local browser/native view allowlists include the route.
The sidebar does not yet expose a rename control.

The native provider CLI contract now requires rename, stale-title and empty-title
rejection, identical saved messages/runs and persisted rename after backend
reopen without inference. JavaScript syntax checks, 70 extension tests and 12
browser tests passed locally. Native execution of the rename addition is
pending; the local build guard again deferred for active sibling benchmarks.
