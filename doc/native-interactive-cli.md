# Interactive native CLI sessions

The C++ CLI connects to the same authenticated local xMind Server used by the
browser and VS Code. The backend owns agents, tools, permissions, sessions and
SQLite through embedded xlang3. The CLI reads command/event contracts and does
not access the database or run a separate agent.

Current source passed **65 native contracts locally in 113.16 seconds**, with
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
Full interactive/TUI coding parity and live Gemini CLI/IDE acceptance remain
incomplete; private team-server implementation belongs to Nexus.

## Conversations, observation and approvals

With `XMIND_AUTH_TOKEN` privately set, run:

```text
xmind_cli PORT chat [SESSION [MODEL]]
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
| `/help`, `/exit` | Show commands or leave between turns; prefix a literal slash request with `//` |

Model-free backends still permit history/run inspection and registered human/tool
graphs. New agent requests reread execution availability; unavailable execution
creates no session, user message or run. Unknown commands/IDs and rejected
admission preserve selection. HTTP admission errors do not fabricate a run or
assistant response and do not trigger resubmission. An unknown transport outcome
requires inspecting recorded runs before an explicit resubmission.

At an actual approval prompt, `/allow ID` and `/deny ID` use only the displayed
owned operation; the backend revalidates its exact plan, arguments and expiry.
At a graph human prompt, `/input NODE_ID JSON` forwards bounded raw JSON with
the displayed checkpoint revision. `/cancel` requests cancellation and
observation continues to the actual terminal event; an acknowledgement is not
a completed cancellation. EOF or `/exit` at either prompt detaches without
approval, human input or cancellation. No approval is automatic.

Input is currently read between turns and at approval/human prompts. Concurrent
stdin controls during model streaming, rich terminal diffs, attachment/context
controls and full TUI/OpenCode CLI parity remain required.

## Generic provider-profile controls

These commands use the backend's public profile registry and advertised routes.
`REVISION` is the registry revision returned by `provider-profiles`, distinct
from an individual profile's saved version. Model identities retain the exact
discovered resource, including Gemini's `models/<id>` prefix.

```text
xmind_cli PORT provider-profiles
xmind_cli PORT profile-models ID ROUTE REVISION [KEY_ENV]
xmind_cli PORT save-profile ID ROUTE MODEL REVISION [KEY_ENV] [--activate]
xmind_cli PORT select-profile ID REVISION
xmind_cli PORT provider-models
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
account discovery, rendered IDE enrollment or end-to-end coding parity.

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
