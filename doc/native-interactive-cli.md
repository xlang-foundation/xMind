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

Current inspected hosted checkpoint `4203b84678b51b2ae7c6ee47fccfa14b684680ae`
passed 52 native, 72 extension and 13 browser contracts in
[run 37707042113](https://github.com/xlang-foundation/xMind/actions/runs/37707042113).
The native provider/CLI contract exercised conversation list/resume/new/rename,
stale and empty title rejection, unchanged history/runs and rename persistence
after reopening, without inference. The HTTP/CLI contract includes request-based
titles and whitespace input behavior. [Exact scope and totals](evidence/native-session-navigation-hosted-provenance.json).
This supersedes the historical pending-compilation notes below for those features;
it does not establish actual editor rename acceptance. The browser preview now
uses that exact checkpoint and exposes the rename capability. Actual browser
rename/Cancel, refresh and native reopen retained the title and connection; the
installed CLI listed/resumed the same completed conversation and restored its
exact history without inference. [Live evidence](evidence/live-browser-cli-navigation-rename.json).
Full interactive/TUI parity remains incomplete. Earlier installation notes below
retain their historical checkpoint scope.

New source adds `/watch RUN_ID` inside chat. It reads the existing single-agent
run, restores its conversation history and emits `run_attached`, then observes
durable events and explicit pending approvals through the existing native client
path. It sends no model admission request. An empty chat adopts the recorded
conversation; a selected different conversation is rejected until the user
selects the correct one or clears selection with `/new`. Missing/invalid IDs and
graph roots/children are rejected without changing selection; graph observation
continues through `graph-watch`. Terminal observation updates the actual last
turn exit status. EOF at an approval still detaches without a decision.

The native HTTP/CLI contract now checks completed attachment and unchanged
history/runs/provider request counts, plus invalid, absent and foreign-context
rejection. The edit contract now detaches from a real pending operation, attaches
the same run in another CLI process and denies that exact operation, preserving
the actual file and run count. Inference in those contracts is labelled
synthetic. JavaScript syntax and whitespace checks passed; compiled execution
is pending because the local build guard detected active xlang3 benchmarks.
This source is not installed in the preview. Concurrent stdin controls while
model output streams remain required; `/watch` does not close that gap.

Chat inspection no longer requires an available model in newer source. The CLI
can list/resume saved conversations, read history and attach existing runs on a
model-free server. Before each new prompt it rereads native execution capability;
when unavailable it reports the setup requirement without creating a session,
appending input or admitting a run. This also allows a later successful backend
configuration to be recognized without reopening chat. Explicit initial model
IDs still require backend catalogue validation. The model-free HTTP/CLI contract
checks saved-history inspection and unchanged sessions/history/runs after rejected
input, including an empty chat that must create no placeholder conversation.
These additions passed JavaScript syntax and whitespace checks; compiled
execution and actual console acceptance remain pending.

Installed preview update: the browser backend and matching adapter now use
verified checkpoint `46262d6`, which passed 52 native, 73 extension and 14 browser
contracts. It includes the earlier navigation/title capabilities and the newer
cookie-duration/MCP diagnostic changes. The `/watch` and model-free inspection
additions were introduced after this source and still await their own compiled
gate; they are not yet present in the installed CLI. The native CLI on `46262d6`
also carried the exact approval for the completed live external MCP read.
[Gate scope](evidence/native-model-protocol-diagnostics-hosted-provenance.json),
[live MCP scope](evidence/live-responses-mcp-diagnostic-read.json).

New graph-control source adds `/graph-watch ROOT_ID` inside chat. It attaches a
recorded root without graph admission, restores the conversation, and validates
root/child ownership while replaying durable events. A model-free server can
still expose registered human/tool graphs. Pending steps emit escaped
`graph_human_review` records with actual prompts, node IDs and the displayed
checkpoint revision. Enter `/input NODE_ID JSON`, `/cancel`, or `/exit` at that
prompt. Raw bounded JSON is forwarded as `input_json` with that exact revision;
an invalid, stale or already-answered step is not retried automatically.

Child effect operations are read from validated children and retain the same
exact `/allow ID` and `/deny ID` path. Graph root cancellation is explicit;
EOF or `/exit` at a human/approval prompt detaches without granting an effect,
answering a step or cancelling backend ownership. Completed/failed/cancelled
observation reports the actual terminal exit status. Single-agent/graph and
selected-conversation boundaries remain enforced.

The graph service contract now exercises actual model-free CLI attachment,
foreign-node rejection, human answer delivery, a stale-answer race against a
second client, detach/reconnect, root cancellation and approved creation through
a child operation. Human inputs and inference replies remain labelled fixtures;
filesystem/backend effects are real when compiled execution runs. JavaScript
syntax and whitespace checks passed. Local compilation was deferred by a live
xlang3 benchmark; hosted execution and installation of this source remain
pending. Concurrent input during model streaming and full console/TUI parity
remain required.

Initial recovery checkpoint `38cd516` compiled but failed its hosted native gate:
50 contracts passed and two failed request-count assertions. The cancelled-run
attachment case compared a global provider count while an unrelated queued read
was allowed to execute. It now checks that cancelled task's unique prompt count,
unchanged run records and absence of new admission output. The edit case retained
the older 48-request total after adding a resumed denial's normal continuation.
That continuation is now checked locally in the scenario; the suite total adds
explicitly verified new console scenarios to the original baseline.
[Initial failed gate](evidence/native-cli-recovery-initial-failure-provenance.json),
[unaltered failure excerpts](evidence/native-cli-recovery-initial-failure.log).
No runtime artifact was published from that failed gate. These corrections still
await compiled rerun; the running preview remains on verified `46262d6`.

Cancellation acknowledgements now use `run_cancel_result`, separating the
server's `cancellation_requested` receipt from effect decisions and human answers.
Observation continues until the actual terminal event; a receipt is not a
completed-cancellation state. New fixtures check the record type, unchanged file
and cancelled operation/run for single-agent approval cancellation and graph
human-step cancellation. Syntax checks passed; execution remains pending.

Interactive chat now also implements `/graphs` and `/graph GRAPH_ID REQUEST` in
source. The catalogue comes from the selected native backend; admission uses
that registered graph's current revision, the selected conversation and optional
selected model. Unknown graphs, blank requests and unavailable graph definitions
are rejected before creating a conversation. Tool-only graphs can start without
a configured provider. Once admitted, the same graph watcher displays persisted
root/child events, exact effect approvals and revision-bound human input. Closing
the client leaves server ownership intact. Admission errors do not trigger a
second submission.

The compiled graph-service fixture now requires catalogue/rejection to preserve
the conversation catalogue and a model-free console launch to execute an actual
workspace read, retain its registered revision and restore the exact history.
JavaScript syntax and whitespace checks passed. Local compilation was deferred
by live sibling benchmark process 17832; hosted execution of this addition is
pending. This is registered graph execution, not dynamic graph generation or a
completed console/TUI parity claim.

`/runs` now lists the selected conversation's recorded root runs in source,
including their actual backend state and graph-root identity. An empty selection
returns an empty catalogue without creating a conversation. This closes the
console navigation path `/sessions` → `/session ID` → `/runs` → `/watch ID` or
`/graph-watch ID`. Graph children remain available through the existing separate
graph-child commands. The graph-launch fixture was corrected to check this
repository contract: one root in the session catalogue and one actual read child
under that root, rather than expecting children in the root catalogue.

Native fixtures require exact saved run records after single-agent attachment,
graph completion and provider-free conversation inspection, with no additional
inference or admission. Syntax checks passed; compiled execution is pending.

Run-admission rejections with HTTP 400, 404, 409, 429 or 503 now keep interactive
chat open in source. A `run_rejected` record retains the exact requested prompt,
selected conversation, optional graph ID and actual HTTP status. No assistant
message, terminal event or run descriptor is fabricated; raw error bodies are
not printed. The user may inspect `/runs` and `/history`, attach existing work,
or explicitly submit another request. Exit status remains 1 until a subsequent
observed turn changes it. There is no automatic retry. If transport fails before
an admission response is received, the client reports an unknown outcome and
asks the user to inspect recorded runs before resubmission.

The graph-service fixture now checks actual HTTP 409 from a second graph request
in a conversation already owned by a paused graph, continued inspection in that
same console process, exact retained request and unchanged history, root runs
and graph events. JavaScript syntax checks passed; native execution is pending.

The earlier graph-control gate at `51d731d` subsequently completed with 50 native
contracts passing and the same two request-count failures already corrected in
`add0a12`. Its graph-service HTTP/CLI contract passed, including interactive
attachment, human input/stale conflict, child effect approval and root
cancellation. [Exact gate provenance](evidence/native-cli-graph-control-initial-gate-provenance.json)
and [unaltered excerpts](evidence/native-cli-graph-control-initial-gate.log)
preserve that scope. Extension/browser gates were skipped and no runtime bundle
was published. The corrected full gate `37713082833` at `37e153b` is now running;
later console changes and live acceptance remain pending. The preview stays on
verified `46262d6`.

The corrected `37e153b` hosted gate then built and passed 51 of 52 native
contracts, including the CLI recovery, graph launch/navigation/rejection and
cancellation receipt cases. It failed `native_process_executor_contract` when
the Node wrapper timed out its compiled child at 45 seconds. The artifact has
no phase trace identifying where that child stalled, so the cause is unproven.
[Exact failed gate provenance](evidence/native-cli-recovery-corrected-gate-failure-provenance.json)
and [unaltered excerpts](evidence/native-cli-recovery-corrected-gate-failure.log)
record the result. No runtime bundle was published; extension/browser gates were
skipped and the preview was not upgraded. The complete local 52-contract pass at
the later credential-header checkpoint is recorded separately with its local
compatibility-branch runtime provenance. Neither local success nor the CLI
contracts' hosted passes establish a successful overall hosted release gate.

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
The shared sidebar control is described below; the installed preview does not
yet advertise the newer backend capability.

The native provider CLI contract now requires rename, stale-title and empty-title
rejection, identical saved messages/runs and persisted rename after backend
reopen without inference. JavaScript syntax checks, 70 extension tests and 12
browser tests passed locally. Native execution of the rename addition is
pending; the local build guard again deferred for active sibling benchmarks.

The browser and VS Code right sidebar now contain a capability-gated SVG rename
button and title dialog in source. The original selected ID/title remain bound
to the draft; external metadata refresh cannot silently replace its expected
title. A conflict preserves the draft and explains how to refresh/reopen.
Changing conversations closes and clears the dialog; Cancel submits nothing
and the message composer remains intact. Both access adapters reject attempts
to rename another conversation, then forward the same native request and
refresh only the session catalogue. Older backends keep the button hidden.

All 72 extension and 13 browser adapter/DOM tests passed, including extension
host conflict/selection handling and renderer/browser controller behavior. These
are labelled fixtures, not proof of actual IDE/native rename execution. Native
compilation and actual browser/IDE acceptance remain pending. Other previews
and their existing settings/drafts were not reloaded for these source tests.
