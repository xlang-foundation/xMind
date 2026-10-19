# Interactive native CLI sessions

The C++ CLI connects to the same authenticated local xMind Server used by the
browser and VS Code. The backend owns agents, tools, permissions, sessions and
SQLite through embedded xlang3. The CLI reads command/event contracts and does
not access the database or run a separate agent.

The [product design](runtime-product-design.md) consolidates these roles into
`xmind serve` and `xmind [--port PORT] [COMMAND ...]`. The native `xmind` target
shares the server/console/admin/schema handlers. On Windows, running the console
without `--port` discovers the managed profile for the selected workspace or
starts its backend with the same `xmind.exe`; browser and VS Code attach to that
profile. The default workspace is the current directory, overridable with
`--workspace`. Explicit `--port` remains an operator-selected external backend.
Managed profile startup on other operating systems remains unverified;
agent-worker execution is pending.
No legacy profile migration is required.

## Recovering a command with an uncertain outcome

The console source supports the backend's process reconciliation contract in
command mode and chat. This addition is under native validation; installed
0.1.49 does not contain it yet.

Inspect the saved operation, its complete stdout/stderr, and the workspace
before acknowledging possible side effects. Use the same workspace and
`--profile-root` options as the running backend. For an explicitly selected
backend, use its native `--port` with the existing private authentication.

```text
xmind [connection options] operation OPERATION_ID
xmind [connection options] process-output OPERATION_ID stdout
xmind [connection options] process-output OPERATION_ID stderr
xmind [connection options] reconcile-process OPERATION_ID --acknowledge-possible-side-effects "Inspected workspace and saved command output; reviewed possible side effects."
```

In chat, the rest of the line is the audit note:

```text
/reconcile-process OPERATION_ID --acknowledge-possible-side-effects Inspected workspace and saved command output; reviewed possible side effects.
```

The explicit acknowledgment and a single-line note of 16–1,024 UTF-8 bytes are
required. The backend accepts only an uncertain process whose owner has failed
or been cancelled, whose process tree is confirmed retired, and whose saved
output is complete. It records the audit note and marks that operation failed,
releasing its quarantine claim. Other unresolved operations can still block
effects. Reconciliation neither reruns a command nor changes its run to success;
repeating the acknowledgment is rejected. A later task is a separate decision.

The installed 0.1.38 unified CLI has attached to the existing browser backend
without starting a replacement or supplying a manual access token. It reads
the same saved conversation and completes an actual configured-Claude task
through `run` and `watch`: xlang3 executes FastAPI API assertions and exits
zero. The webpage then restores all 36 messages and 14 supplied usage records.
This is installed noninteractive command/stream acceptance.
[Exact shared-backend scope](evidence/shared-cli-browser-coding-0.1.38-20261019.json).
Separately, direct Windows console resume displays the existing human-readable
conversation and actual token metrics, accepts `/runs`, and exits with `/exit`.
All 36 stored messages and the run catalogue stay unchanged on the same backend.
The initial nested-harness attempt emitted JSON rather than human history and
remains a failed observer result; the direct pseudoterminal captures are separate.
Visible desktop rendering, direct-console resize and full-screen terminal
acceptance are not claimed. [Console resume evidence](evidence/console-resume-0.1.38-20261019.json).

Managed profiles accept `--mcp-config FILE` for a trusted MCP server document.
The native backend persists its server list; reconnecting without that option
uses the saved list and does not reread the source file. To apply a changed path
or file content, supply `--mcp-config FILE --upgrade-backend`. The protected
handoff retains the same SQLite profile and history. Malformed documents are
rejected before the existing owner is quiesced. No MCP commands, endpoints or
credentials come from model or page input. [Server document](native-mcp.md).
If the existing owner predates the protected MCP preflight endpoint, first
upgrade it without `--mcp-config`, then apply the MCP document. The older
owner cannot validate this update; xMind leaves it running rather than retire
it with an unvalidated server configuration.

An earlier full native Release gate passed **118 local CTest contracts**.
That historical VCS checkpoint added workspace-scoped Git metadata and local branch listing
alongside status. Backend-owner HTTP coverage exercises a real temporary Git
repository, the local-view adapter, and xlang3-backed SQLite. CLI metadata and
status contracts validate authenticated endpoint selection and reject malformed
responses. This focused run does not establish the full xMind VCS workflow or
a rendered IDE check.

`agents` lists persisted named-agent IDs, revisions and optional model IDs.
`agent-definition ID` retrieves one persisted definition, including its
instructions, through the authenticated native API. Definition IDs may contain
letters, numbers, `_`, `-` and `.`. This inspection is read-only and does not
start a run. The current persisted definition schema does not yet include mode,
permission, description, visibility or source metadata.

## Restore an approved file change

`restore SESSION OPERATION_ID` starts a native restore graph for a successful
file operation from that conversation. The backend revalidates the saved
operation and proposes its inverse through the ordinary approval journal; the
CLI does not write the inverse directly or bypass review. Inspect the new run
and approve its proposed operation through the existing operation controls.
The graph restore contract exercises this command against the real xlang3/SQLite
backend and verifies that the file stays unchanged until approval.

## Delete a conversation

`delete-session SESSION` hides a settled conversation from the shared session
catalog and normal history APIs. Its immutable context and audit records remain
in SQLite. Deletion is rejected while a run, unresolved operation, context
owner, compaction, or manual context request is active. A forked child remains
usable after its source is removed; its parent link is cleared. The equivalent authenticated native endpoint is
`DELETE /v1/sessions/{session_id}`.

## Selected workspace connection

Use an explicit workspace when attaching the console to a local backend:

```powershell
xmind --port 8765 --workspace D:\Projects\Example sessions
xmind --workspace D:\Projects\Example --port 8765 chat
```

The native console opens that directory and pins its OS file identity and
canonical root. Before each request it reads authenticated backend workspace
metadata and verifies the selected root and the originally observed authority.
A different root, malformed metadata or changed authority rejects the command;
the console does not silently adopt the replacement backend. Agent and graph
requests also carry the pinned workspace and authority for atomic backend
admission. Client exit still leaves backend execution alive.

Managed profile discovery/startup is verified on Windows. Explicit `--port`
commands without `--workspace` retain the operator-selected backend behavior.

## Git status

`xmind git-status` reads the selected backend's opened workspace through the
native `GET /v1/vcs/status` endpoint and prints validated JSON file statuses and
line counts. It does not accept a caller-selected path, so managed profiles,
the browser, and VS Code use the same workspace authority. See
[CLI Git status evidence](evidence/cli-git-status-local-20261011.json)
and the [native Git status contract](evidence/native-vcs-status-local-20261011.json).

`xmind git-info` returns the Git provider and current/default branch metadata;
`xmind git-branches` lists local branches. Both use the opened workspace and
the same native backend. They do not accept a caller-supplied repository path.

`xmind git-base` reports an inferred review base, when the selected branch's
remote default or creation reflog identifies one. `xmind git-diff MODE [BASE
[CONTEXT]]` prints validated per-file patches for `working`, `branch` or
`committed` comparisons. It uses the same opened workspace, supports an
explicit base for ambiguous branch history, and bounds patch responses at 4 MiB.
The CLI contract checks authenticated routing and malformed-reply rejection;
the actual Git branch and patch behavior is exercised by the backend HTTP
contract. Rename/binary review rendering is not included yet.

## Multiline requests

Use `/compose` to enter a multiline agent request, or `/compose GRAPH_ID` for
an explicitly registered graph. End the block with `/send` or abandon it with
`/discard`. Data lines retain their newline, including blank lines. `/exit` and
other slash-prefixed lines inside the block are request text. To include the
terminator lines literally, enter `//send` or `//discard`.

```text
xMind > /compose
Review this function:

int add(int a, int b) {
    return a + b;
}
/send
```

The CLI submits one request through the existing authenticated backend. File
effects still require their own approvals. EOF without `/send`, empty content,
NUL-containing content and blocks over 1 MiB are discarded without creating a
session or run. Unknown or unavailable graph selection does not fall back to
single-agent execution. Validation of the new candidate is recorded separately
from the historical checkpoints below.

## Historical session-skill checkpoint

The session-skill CLI source passed the complete **92 native contracts locally in
175.20 seconds**, with all 612 frozen inputs unchanged and no excluded tests.
It adds actual session skill inspection, attachment, removal and clearing through
the authenticated native backend. Unicode arguments and snapshot files, stale
revision rejection and concurrent-client changes passed in both provider-enabled
and model-free modes. These tests use synthetic guides/provider peers and admit
no inference runs. Hosted packaging and installed CLI acceptance for these new
commands remain pending.
[Current CLI evidence](evidence/native-cli-user-skills-local.json).

The earlier provider-profile checkpoint passed **65 native contracts locally in 113.16 seconds**, with
the exact manifest matched, zero failures/skips and no post-build exclusions.
The new provider-profile CLI contract passed in **4.23 seconds**, and actual
browser/native integration passed again against the freshly rebuilt server.
[Exact local scope](evidence/native-provider-profile-cli-local-provenance.json).

The preceding enrollment revision `2f5e0f0` separately passed its hosted gate:
**64 native contracts in 158.09 seconds**, **94 extension and 17 browser tests**,
native/browser integration and VSIX verification, with zero failures/skips.
[Exact hosted scope](evidence/native-gemini-enrollment-hosted-provenance.json).
The frontend sources are unchanged from that revision; those frontend tests
were not repeated or counted as new results in the local 65-contract gate.
Hosted verification of the newer CLI checkpoint remains pending.

The installed browser preview retains native `19d69dd` and view `6f32d215`
snapshots. It has not been upgraded to the newer enrollment/profile CLI source.
Earlier installed CLI/browser checks below retain their historical scope.
[Current validation](VALIDATION_STATUS.md), [provider setup](provider-setup.md).
The CLI has passed one current-profile, approval-gated TestProj edit followed
by two successful xlang3 checks and a hash-verified restore. Broader interactive
coding and full-screen terminal acceptance, including live Gemini CLI coverage,
remain open. [Exact TestProj run evidence](evidence/xmind-cli-approval-gated-coding-20261017.json).
Private team-server implementation belongs to Nexus.

## Conversations, observation and approvals

Session skill controls use the same native API as the browser and VS Code.
Their complete 92-contract local compile/execution gate passed. The first
preflight deferred while a separate xlang3 benchmark was active; no native
execution occurred during that attempt. A later gate passed 91 of 92 tests:
the stale-write diagnostic omitted its HTTP status. The corrected product output
reports HTTP 409 and no automatic retry; the full successful gate retains the
original assertion. Both earlier attempts remain recorded in the evidence.

Interactive commands are `/session-skills`, `/attach-skill ID`,
`/remove-skill ID` and `/clear-skills`. An id can include spaces and Unicode and
resolves through the backend catalogue. Attaching before the first prompt creates
a real empty conversation. The console keeps its observed revision and workspace
identity and does not replay a rejected stale write. Use `/session-skills` and
`/skills` to inspect current state before retrying. A later acknowledged write
clears the failed skill-command exit result; a read alone does not.

Scripted clients can inspect `session-skills SESSION`, save its public JSON
snapshot, then call `set-skills SESSION SNAPSHOT_JSON_FILE [ID...]`. An empty id
list clears the observed selection. The snapshot contains public selection and
workspace metadata, not provider configuration or keys. UTF-8 snapshots and
Windows UTF-16LE snapshots with a BOM are supported by the new source. Snapshot
session identity, bounds, duplicate fields, revision, workspace and acknowledgement
are checked; a newer backend selection is never silently adopted. Windows CLI
arguments use the Unicode entry point and explicit UTF-8 conversion.

With `XMIND_AUTH_TOKEN` privately set, run:

```text
xmind --port PORT chat [SESSION [MODEL]]
```

Chat validates a supplied session and optional backend-enabled model, then
displays saved history before accepting input at `xMind >`. An empty chat
creates a conversation only after its first nonempty request. Whitespace, setup,
inspection and immediate `/exit` do not create placeholder conversations.

Standard output contains escaped NDJSON descriptors and actual durable backend
events, including tool results and supplied usage. Prompts and connection
diagnostics use standard error. A failed turn reports its recorded failure;
the user can explicitly submit another request without automatic retry. Ending
or interrupting the client leaves backend execution ownership unchanged.

| Command | Behavior |
| --- | --- |
| `/sessions`, `/session ID`, `/new` | List/resume saved conversations or clear selection; `/new` creates no database session until the next request |
| `/history`, `/runs` | Read the selected conversation's saved history and root runs without inference |
| `/title NAME` | Rename the selected conversation with its observed title as the conflict precondition |
| `/models`, `/model ID`, `/model` | Inspect backend-enabled execution models, select an advertised model for this chat, or clear the local override |
| `/watch RUN_ID`, `/graph-watch ROOT_ID` | Attach existing single-agent or graph work and replay durable state without admitting another run |
| `/graphs`, `/graph GRAPH_ID REQUEST` | Inspect registered backend graphs or explicitly admit one at its observed catalogue revision |
| `/commands`, `/command-id ARGUMENTS` | List workspace Markdown commands or invoke an exact ID from `.agents/commands/` through the native command route |
| `/help`, `/exit`, `/cancel` | Show commands or leave; during an active run `/cancel` requests backend cancellation and `/exit` detaches without cancelling. Prefix a literal slash request with `//` |

Model-free backends still permit history/run inspection and registered human/tool
graphs. New agent requests reread execution availability; unavailable execution
creates no session, user message or run. Unknown commands/IDs and rejected
admission preserve selection. HTTP admission errors do not fabricate a run or
assistant response and do not trigger resubmission. An unknown transport outcome
requires inspecting recorded runs before an explicit resubmission.

Chat keeps a bounded input queue active while a run streams. `/cancel` sends one
backend cancellation request and continues observing until the server records a
terminal state. `/exit` detaches immediately and leaves the server-owned run
untouched. Ordinary prompt lines entered during an active run are saved to the
durable session inbox. Once the run ends, the CLI admits queued prompts in order
through the regular run API; SQLite atomically claims each prompt with the run
and writes it to conversation history exactly once. Slash commands remain queued
for normal processing after the active turn. Closed scripted input is drained as
commands instead of being interpreted as a live cancellation or detach request.

At an actual approval prompt, `/allow ID` and `/deny ID` use only the displayed
owned operation; the backend revalidates its exact plan, arguments and expiry.
At a graph human prompt, `/input NODE_ID JSON` forwards bounded raw JSON with
the displayed checkpoint revision. `/cancel` requests cancellation and
observation continues to the actual terminal event; an acknowledgement is not
a completed cancellation. EOF or `/exit` at either prompt detaches without
approval, human input or cancellation. No approval is automatic.

`planning` reports the authenticated backend's current AgentFlow planning
capability. `inspect-plan RUN` prints the same formatted JSON observation used
by the browser and VS Code clients, including nested plan owners, topology,
accepted call/revision ledgers and any available held-response metrics.
Provider receipts and private capability material remain filtered by the
native endpoint.

File approvals now render bounded unified diffs; the focused native agent HTTP
contract also exercises live-process cancellation and detach against an active
synthetic stream. Interactive diff navigation, attachment/context controls
during streaming and full-screen terminal behavior remain to be validated.

## Workspace shell attachment

`terminal-attach SESSION [TITLE] [--console]` creates a native terminal pinned to the
selected server workspace and the chosen persisted conversation. The CLI
streams exact PTY bytes and forwards console key events as VT input, including
Unicode, control keys, navigation and function-key sequences. Console resize
events resize the backend pseudoterminal, and original console input/output
settings are restored on return. Pipe-based invocation uses UTF-8 lines for the
native server contract. A nested Windows ConPTY integration contract exercises
console keystrokes and live resize. Full-screen application behavior and mouse
input remain unverified; concurrent chat controls, rich diff navigation and
complete full-screen terminal behavior also remain open. Use `--console` when a
launcher redirects standard input while the process still has an attached
Windows console.

The actual-server process contract starts the shell in an isolated fixture,
sends `echo XMIND_CLI_TERMINAL_ATTACH_OK` and `exit`, checks relayed output and
exit status, and verifies that the terminal was removed from its conversation.
The fixture never targets the user's workspace.

## Generic provider-profile controls

These commands use the backend's public profile registry and advertised routes.
`REVISION` is the registry revision returned by `provider-profiles`, distinct
from an individual profile's saved version. Model identities retain the exact
discovered resource, including Gemini's `models/<id>` prefix.

```text
xmind --port PORT provider-profiles
xmind --port PORT profile-models ID ROUTE REVISION [KEY_ENV]
xmind --port PORT save-profile ID ROUTE MODEL REVISION [KEY_ENV] [--activate]
xmind --port PORT select-profile ID REVISION
xmind --port PORT provider-models
```

`provider-profiles` reads actual public metadata: registry revision, active ID,
saved profiles and backend-owned route/discovery declarations. `profile-models`
discovers account models for the specified profile/route/revision.
`save-profile` publishes a validated profile; it stays inactive unless
`--activate` is explicitly supplied or it updates the already active profile.
The optional flag must be the final argument; alternate ordering is rejected.
`select-profile` explicitly changes the shared active profile using the supplied
revision.

Only the environment-variable name `KEY_ENV` appears in command arguments.
The CLI consumes its privately inherited value and forwards the key only in the
authenticated setup request; the backend owns encrypted credential persistence
through xlang3 SQLite. Keys, credential references and provider destinations are
excluded from public metadata/output. Authentication/reserved UI environment
variables cannot be used as provider-key variables. On Windows, consumption
clears that variable in the child CLI process, preserving the parent environment.
Across all advertised routes, the backend rejects profile/model identities that
reflect the private key during draft/save/import/reopen validation and checks the
profile identity before account discovery. Rejection precedes public publication
or candidate credential persistence; discovery sends no request for such an ID.

Omitting `KEY_ENV` omits `api_key` from the request and requests the saved
profile's owned credential. New profiles still require a key. Draft discovery
does not persist one; save and selection use native registry CAS and execution
ownership checks. Setup/discovery/profile controls create no conversations,
user messages or agent runs and do not invoke inference. Backend validation,
stale revisions and running/paused ownership conflicts are reported without
automatic retry or partial client-side publication.

No-argument `provider-models` prefers the active profile's saved-key discovery
on a profile-capable backend. The older
`provider-models KEY_ENV REVISION` form remains the legacy setup API; it is not
a generic provider-profile enrollment command. The legacy
`configure-provider MODEL KEY_ENV REVISION` retains its existing scope.

### Live managed-profile discovery — 2026-10-17

The managed CLI attached to the TestProj local profile without key entry.
`provider-models` reused its saved OpenAI credential and returned 20 account
models. `models` returned one backend-enabled execution model,
`gpt-4.1-mini`; that command's result is intentionally narrower than provider
account discovery. Neither command submitted inference. Exact binary, model
catalogues and limitations are recorded in the [live evidence](evidence/cli-openai-model-discovery-20261017.json).

Between chat turns, the new controls are:

| Command | Profile/admission effect |
| --- | --- |
| `/profiles` | Display fresh public registry metadata without silently rebinding this chat |
| `/profile ID REVISION` | Explicitly select with registry CAS, then bind the returned committed metadata and clear the local model override |
| `/provider-models` | Discover the freshly observed active profile/route with its owned key, without rebinding existing chat admission |

If another view changes the shared profile, listing or discovery does not acquire
that new admission binding. A stale profile command reports failure and retains
the previous binding/model selection. An explicit successful `/profile` action
is required to replace the chat binding; tasks are never automatically retried.
Successful settings-only commands clear only a prior settings error and preserve
the last failed/cancelled model turn's exit status. A later actual successful run
can recover that turn status. Discovery uses a 35-second client read deadline,
then restores ordinary request timing; it follows no redirects and sends no retry.

Profile selection changes future execution and preserves existing conversation
records. Fresh conversations and compatible text histories can use the selected
wire. Foreign signed/provider/tool receipts require their original native wire
mapping: for example, a Gemini receipt cannot be silently rewritten for Chat or
Claude, and older Chat tool calls lack the original Gemini receipt needed for
GenerateContent replay. Incompatible histories are rejected explicitly rather
than guessing signatures or replaying tools. Use `/new` for a fresh conversation
or restore a compatible provider for the recorded history. Arbitrary cross-wire
history conversion remains native acceptance work.

The compiled local gate covers exact command syntax, private environment input,
omitted-key reuse, inactive enrollment, explicit selection, saved-key updates,
encrypted SQLite restart/rollback and unchanged setup session/history/run state.
Independent synthetic provider peers exercise OpenAI/Claude/Gemini discovery,
full model resources, sanitized diagnostics and external-change races. Actual
native Gemini text execution and signed-history reopen also passed. Regressions
verify the discovery deadline and failed-turn status through successful settings
commands. These results establish the native CLI/backend contracts, with hosted
verification still pending for this checkpoint. They do not establish live
account discovery, rendered IDE enrollment or the complete xMind coding flow.

## Historical checkpoint and live evidence

The records here apply to their exact revisions and dates. Earlier preview
versions and pending-compilation statements are not the current source or
installed-preview status.

- `51724ab8fdda3b9349b18f110a1256ec4f1398eb` passed hosted 52 native/63 extension
  contracts for initial shared-session chat and restart history.
  [Exact evidence](evidence/native-interactive-chat-hosted-provenance.json).
  Its separately installed CLI passed an empty launch against the existing backend,
  without inference or a new session.
  [Installed scope](evidence/native-interactive-chat-local-installed.json).
- `d0a70fef888c3724c2490bcb2cf8ef38d60f08c0` passed hosted 52 native/66 extension
  contracts, adding resumed history, failure recovery, slash/model/history controls
  and explicit approval/denial, unrelated-ID rejection and detach behavior.
  [Exact scope](evidence/native-cli-approval-diagnostics-hosted-provenance.json).
- `d3a395d89a1ecf93947728338a65822c441464bd` passed hosted 52 native/66 extension
  contracts, including initial-model prevalidation and the original OpenAI
  saved-key `/provider-models` path.
  [Exact scope](evidence/native-cli-discovery-setup-hosted-provenance.json).
  It does not prove the newer generic-profile path.
- `4203b84678b51b2ae7c6ee47fccfa14b684680ae` passed hosted 52 native/72 extension/
  13 browser contracts, including native list/resume/new/rename, stale/empty title
  rejection and unchanged durable history/runs.
  [Exact scope](evidence/native-session-navigation-hosted-provenance.json).
  Actual browser rename/Cancel, refresh/reopen and installed CLI navigation have
  their own [live scope](evidence/live-browser-cli-navigation-rename.json).
- `46262d6` passed hosted 52 native/73 extension/14 browser contracts and carried
  an actual approved external MCP read through the CLI.
  [Gate](evidence/native-model-protocol-diagnostics-hosted-provenance.json),
  [live MCP scope](evidence/live-responses-mcp-diagnostic-read.json).
- `5cf3c79` passed hosted 52 native/73 extension/14 browser contracts plus
  packaging, and was installed in the browser preview at that checkpoint.
  [Exact gate](evidence/native-cli-recovery-provider-headers-hosted-provenance.json),
  [original summary](evidence/native-cli-recovery-provider-headers-hosted-summary.log).
  Its later actual console `/graph read.repository.file ...` and
  `/input choose.file {"path":"README.md"}` read 11,810 bytes matching the observed
  file, with matching graph/history/root-run inspection and no inference.
  [Historical live read](evidence/live-console-registered-graph-read.json).

Failed gates remain evidence too. `38cd516` passed 50 native contracts but failed
two request-count assertions; their corrected ownership/count scenarios are
described in [the original failure](evidence/native-cli-recovery-initial-failure-provenance.json).
The `51d731d` graph-control gate passed its graph-service contract but retained
those failures; frontend gates were skipped and no runtime was published.
[Exact failed scope](evidence/native-cli-graph-control-initial-gate-provenance.json).
The corrected `37e153b` passed 51 of 52 native contracts but timed out the process
fixture at 45 seconds; its stall phase/cause remains unproven.
[Exact failure](evidence/native-cli-recovery-corrected-gate-failure-provenance.json).
The later `5cf3c79` full success does not explain that earlier timeout.

`Tools/agentflow.ps1 -Action Chat -Port PORT` remains the public chat launcher,
with optional `-Session ID`, `-Model ID` and `-BinaryDirectory DIR`. It forwards
native arguments and inherits private authentication; it copies no provider key
or database. Its earlier empty-chat and explicit Responses-wire forwarding checks
retain their [installed-binary scope](evidence/native-interactive-chat-local-installed.json).
