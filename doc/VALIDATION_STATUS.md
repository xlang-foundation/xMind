# Current native validation

## Live browser coding acceptance — 2026-10-15

From the rendered local browser UI, the current TestProj workspace created the
reviewed `xmind-ui-folder-smoke/hello_world.py` through the normal approval
flow, then ran it through the admitted xlang3 process profile. The recorded
executable was the profile-bound `xlang3.exe`, with arguments `-B
hello_world.py` and working directory `xmind-ui-folder-smoke`. The process
exited 0, wrote `xMind browser coding smoke passed.` to stdout, and wrote
nothing to stderr. I read the file back and confirmed the target folder contains
only that file. Exact run and operation IDs are in
[browser and VS Code evidence](evidence/browser-vscode-contracts-20261015.json).
This validates one end-to-end browser file proposal, approval, and xlang3
execution on TestProj; it does not yet cover a multi-step coding/build task.

The same xMind local browser client then ran `pytest -q -p no:cacheprovider
tests/test_api.py` from `model-zoo/backend` through its xlang3 process profile,
with `-B` and the existing project-local `.xlang3-site`. All nine FastAPI
contracts passed in 2.27 seconds; pytest emitted one Starlette/AnyIO deprecation
warning and no stderr. The process took 4.34 seconds, exited 0, and the backend
entries retained their earlier timestamps. No CPython, shell wrapper, network
install or source edits were used. The existing Model Zoo preview also rendered
the filtered Whisper Small result and its detail page in the Codex in-app
browser; external Chrome is unavailable in this session. See the complete
[TestProj browser evidence](evidence/testproj-xmind-browser-coding-20261015.json).

## Saved-provider sidebar and live inference — 2026-10-15

The rendered browser's saved profiles discovered 13 Claude models, 11 Gemini
models and 2 DeepSeek models without manual model-ID entry. Live requests using
`claude-sonnet-4-6`, `models/gemini-3.8-flash` and `deepseek-flash` returned the
requested exact replies. OpenAI `gpt-5.6-sol` is covered by the live browser
coding run above. Gemini `models/gemini-2.5-flash` was listed but returned HTTP
404 for this key; the same Gemini route succeeded with 3.8 Flash. Google
currently limits 2.5-model access to users who have actively used them, so the
failed selection is recorded as account/model availability rather than a route
failure ([Google model catalogue](https://ai.google.dev/gemini-api/docs/models)).
Per-run timings and IDs are in
[provider sidebar evidence](evidence/provider-sidebar-live-20261015.json).

## Native graph restore — 2026-10-15

The Release graph-runner contract now exercises a registered two-node graph:
create a file in an admitted secondary workspace folder, reference the first
node's actual operation ID from `restore_operation`, review both operations,
and verify the inverse removes only that file. Both proposals retain the exact
workspace membership revision and folder binding. The graph-runner contract
passes with a synthetic model peer (the restore flow itself makes no model
request), and the standalone restore contract passes for update, add, delete,
move and stale-file rejection. Browser and VS Code now expose Restore saved
change actions that start a native restore graph with no model call; the inverse
still follows the normal approval flow. Release native server compilation and
browser/VS Code client suites pass. Rendered installed-client acceptance remains
open.

The Release graph-service HTTP/CLI integration contract now exercises the direct
restore route through the production provider-profile adapter. It verifies
cross-conversation rejection, exact request-schema validation, native graph-run
admission, approval-gated deletion of the created file, and zero provider calls.
This also caught and fixed missing `restore_operation` forwarding in both provider
runtime adapters and updated the CLI validator for the backend's current agent
catalogue metadata.

## Installed VS Code focused coding run — 2026-10-15

The VS Code Extension Development Host opened the TestProj workspace, reused
its saved DeepSeek profile, and applied an approved focused edit to
`model-zoo/frontend/src/views/HomeView.vue`. The parent now listens for the
`ModelCard` `toggle-save` event and passes its model ID to the existing
localStorage-backed save/unsave handler. A separate focused run completed
`npm.cmd run build` through the configured PowerShell process profile with
exit code 0: Vite 6.4.4 transformed 37 modules in 739 ms, with no stderr.
The built preview at `http://127.0.0.1:4173/` returned HTTP 200. Chrome could
not be opened through Windows UI automation (`GetCursorPos` returned access
denied), so rendered external-browser inspection remains pending. TestProj has
no Git root, preventing a full workspace diff. The preceding broad redesign
run timed out with a file approval pending; this checkpoint proves the focused
VS Code edit and build only. See the sanitized
[run evidence](evidence/vscode-testproj-focused-edit-20261015.json).

## VS Code managed-profile edit-policy upgrade — 2026-10-15

An existing local workspace profile can now change between approved-edit and
read-only modes through the explicit trusted-runtime upgrade. The native owner
quiesces and hands off its existing profile, retaining the same loopback origin,
profile directory, SQLite sessions and history. The extension skips its retained
view adapter during that one-shot upgrade and forwards the selected policy;
ordinary reconnects do not silently change an existing profile. The sidebar
offers the apply command when `agentflow.workspaceEdits` changes, and coding
edits continue to use the native approval journal.

The current VS Code extension suite passes **366/366**. The Release native
`native_local_profile_contract` passes in **373.71 seconds**, including both
read-only-to-approved and approved-to-read-only handoffs, stable endpoint and
profile identity, and exact session/history preservation. This fixture-based
contract does not by itself establish that the live Development Host has been
reloaded or that an editor coding task has passed.

## Bidirectional official A2A SDK interoperability — 2026-10-15

Fresh Visual Studio 18 Release binaries built from xMind `2c3b8c7` against
xlang3 `fcecf499` pass both directions of the pinned `@a2a-js/sdk` 1.3.0
interop check and the native registered-agent contract (**3/3 CTests**). The
native outbound client interoperates with an independently implemented
official SDK server peer for authenticated AgentCard discovery, send/get, text
artifact retrieval, streaming and cancellation. A native AgentRunner also
discovers a registered remote-agent tool, binds the exact message and peer
revision to durable approval, records stream events, and verifies denial and
post-dispatch recovery after SQLite reopen without replaying a send. Inbound,
the official SDK client exercises xMind's actual local native server through
discovery, send/get/list/cancel, stream/subscribe,
version compatibility, multipart history, UTF-8 text attachments, task
references and SQLite restart. The server contract also reaches a synthetic
provider endpoint through the native AgentRunner.

This closes the pending hosted native build for these two contracts and
establishes bidirectional SDK wire evidence. The peer and provider are test
fixtures: live third-party agents, other bindings/security modes, installed
VS Code or rendered browser workflows, and complete A2A compliance remain
unverified. Exact revisions, binary hashes, test scope and command are in
[`native-a2a-sdk-bidirectional-20261015.json`](evidence/native-a2a-sdk-bidirectional-20261015.json).

## Current browser and VS Code adapter contracts — 2026-10-15

The current-source VS Code extension host/renderer and browser UI/controller
Node suites pass **487/487** on xMind commit `c1658922ca53e60cfc4ebbd9fc24fd2b72fa335e`:
367 VS Code tests and 120 browser tests, with no
failures, cancellations or skips. The tests cover the current workspace and
session workflows, streamed UI state, file approvals, restore requests,
provider/model selection, graph/planning routes and MCP/A2A client routing.
The live TestProj browser at `http://127.0.0.1:51433/ui/` also rendered the
workspace settings and new-workspace form. Only one workspace was registered,
so the switch selector was disabled; the create form was canceled without
changing the local profile. This verifies the dialog path but not switching
between two live workspaces. The source suites use synthetic host and DOM
fixtures; installed VS Code coding acceptance remains separate. Exact commands
and scope are in
[`browser-vscode-contracts-20261015.json`](evidence/browser-vscode-contracts-20261015.json).

## Approved coding restore — 2026-10-15

The Release server and restore contract build successfully. The new native
restore contract creates real update/add/delete/move operations, approves each
inverse through the normal journal, verifies the exact restored bytes, and
confirms a changed live file is rejected before a restore proposal exists. The
focused coding-edit gate passes **8/8**: create, edit, patch-file, patch-batch,
restore, approved-edit HTTP, and native agent edit/patch HTTP contracts. The
agent tool catalogue exposes `restore_operation` when approved edits are enabled.
The browser and VS Code now expose Restore saved change controls. They start a
native restore graph directly, without asking the model to construct a prompt;
the filesystem inverse still waits for the existing human approval. The direct
restore service and client route contracts pass. Rendered installed-client
acceptance remains open.

## xMind TestProj coding and xlang3 import compatibility — 2026-10-15

The browser UI completed a real TestProj request using the saved Gemini
provider profile and `models/gemini-3.8-flash`. The run invoked the native
`run_process` tool with the xlang3 profile to execute `pytest -q` in
`model-zoo/backend`; the UI recorded exit code 0 and **9 passed, 1 warning**.
This exercised the browser, saved provider configuration, native AgentFlow
backend, workspace selection, and process executor. It was a local development
runtime, not a release-package or installed VS Code acceptance.

xlang3's `xmind-required-compat` branch fixes two import cases found by this
workspace: qualified native package DLLs emitted flat in the module root, and
Windows `LoadLibraryExW` failures for profile paths near `MAX_PATH`. The
patched runtime imports `pydantic_core` from the 254-character TestProj profile
path and runs the backend pytest suite **9/9**. The schema-gather and
near-`MAX_PATH` CTest contracts pass **2/2**. The complete 11-workload paired
performance gate against the preserved pre-fix runtime passes with no case
over the 10% regression limit. The xlang3 fix is isolated on its branch and
does not broaden into general runtime optimization.

The xMind profile recovery change clears a persisted replacement intent only
when the previous owner is observed stopped and has not committed a quiescence
receipt. The local profile contract passed in 268 seconds, including real
SQLite-backed graph history, interrupted-upgrade recovery, and active human
approval admission fencing. After rebuilding the backend-owner contract
binary, the owner and owner-HTTP contracts passed **2/2**; the full focused
native selection passed **4/4**. xMind checkpoint `a77b693` is on `main`; the
xlang3 compatibility checkpoint is `9679dfbd` on `xmind-required-compat`.

Remaining acceptance includes complete installed VS Code coding workflows,
external MCP/A2A interoperability, release packaging, and the wider OpenCode 2
feature-parity audit. These results establish a local browser workflow and a
targeted xlang3 compatibility fix, not completion of the full product goal.

The current VS Code extension regression suite also passes **353/353**. The
running Extension Development Host command line points to this repository's
`extensions/vscode` source and the TestProj workspace; its extension-host log
confirms `agentflow-local.agentflow` activated. The live VS Code window could
not be inspected in this run, so this does not establish current sidebar
rendering or a new editor-driven coding/build acceptance.

## Nested human pause/resume and restart recovery — 2026-10-14

An ordinary root can now pause on a human node owned by an admitted nested
Agent, persist the exact waiting plan/call and root-to-child owner chain, and
pause those owners with the shared active-time segment. The service routes an
authenticated answer to the deepest waiting plan. Resume continues each
original accepted call under its original owner run; SQLite reopen preserves
the clean chain and does not replay provider calls. A synthetic-provider
AgentService contract verifies nested answer/revision/completion and a second
scenario closes/reopens the service while paused before answering. The Release
repository, engine and service contracts pass **3/3**. Provider responses and
credentials are synthetic. Simultaneous nested human waits in sibling root
branches remain unsupported; a nested plan receives a typed pre-acceptance
rejection while another root child is live. [Engine contract](../Native/tests/dynamic_plan_engine_contract.cpp)
and [owner-chain contract](native-dynamic-plan-recursion.md).

## Root-wide nested planning limits — 2026-10-14

Plan acceptance now enforces node-label, accepted-revision and human-question
allowances across all plans owned by one root, inside the serialized SQLite
transaction. A nested owner cannot reset these allowances by opening another
plan. The focused Release repository contract passed **1/1** and includes
separate nested-owner rejection checks for all three limits, verifying that
rejected work leaves no nested plan or partial reservation. The engine
contract separately covers single-chain nested pause/restart; depth-nine
runtime rejection and remaining total-work limits remain open. [Contract
source](../Native/tests/dynamic_plan_repository_contract.cpp).

## Live TestProj coding acceptance — 2026-10-14

The current xMind browser backend admitted and completed a real coding run on
the `D:\CantorAI2026\TestProj` workspace using the saved OpenAI Responses
profile and `gpt-5.6-luna`. The run updated only
`model-zoo/frontend/src/App.vue` and
`model-zoo/frontend/src/components/ModelCard.vue`, strengthening the demo-data
disclosure and labelling illustrative card values. The generated production
build passed, and an independent `npm run build` also passed (37 modules).

For browser acceptance, the ModelZoo frontend was served at
`http://127.0.0.1:4174/` and its FastAPI backend was started with the xlang3
interpreter. Health, catalog statistics, and a filtered model query returned
success. The browser rendered all 12 seeded models, searched for “detection”
and showed the matching three, and opened the ResNet-50 detail route with the
demo-metric disclosure intact. The dev server and API remain available for
manual review. Exact sanitized run details are in
[TestProj coding acceptance](evidence/testproj-modelzoo-coding-acceptance-20261014.json).

The existing local dependency target had Pydantic 2.14.1 paired with xlang3's
built-in `pydantic-core` 2.46.5, so FastAPI could not import. A temporary
Pydantic 2.13.5 pure-Python wheel overlay in the OS temp directory restored a
compatible pairing without changing TestProj backend source or installing a
CPython native extension. xlang3 pip's `--target` install path failed while
iterating package entry points (`TypeError: 'object' object is not iterable`);
the wheel download succeeded. The backend pytest suite remains blocked by
xlang3's `PathAwareHookProxy` missing `pytest_cmdline_main`; the UI/API and
frontend build acceptance above passed independently.

### Follow-up with current xlang3 branch build

The failures above came from the stale `build/Release/xlang3.exe` dated
2026-10-12, not the current `fix/pytest-pathaware-hook-proxy` source. Building
that branch into its separate `build-pytest-pathaware/Release` directory made
the existing descriptor/pytest compatibility fixes available without stopping
the xMind UI or replacing the other runtime. With the current interpreter,
`pip install --target <temp> --no-deps pydantic==2.14.1` installed the
pure-Python wheel successfully and paired it with xlang3's built-in
`pydantic-core 2.50.1`. The TestProj FastAPI backend suite then passed **9/9**
with one deprecation warning. The API was restarted on port 8000 using this
current branch build; health, catalog stats, the three-result detection query,
and the browser's 12-model catalog all passed. The temporary runtime path puts
the branch's native `modules` directory ahead of the project package target so
the xlang3-provided Pydantic Core facade and `_schema_gather` source resolve as
a matched package. Exact results are in
[current xlang3 TestProj acceptance](evidence/testproj-xlang3-current-branch-acceptance-20261014.json).

## Current-source browser profile and model catalogue — 2026-10-14

The current native Release executable was paired with the verified xlang3
runtime DLL and staged as a hash-verified, local-only browser development
overlay. The browser opened at `http://127.0.0.1:60415/ui/` on the TestProj
workspace. Its same-origin adapter created a local view session without
requesting a server token; the workspace catalogue returned TestProj and
allowed workspace addition. The browser received the four saved provider
profiles. Native account-model discovery returned 13 Claude, 11 Gemini, 2
DeepSeek and 20 OpenAI model IDs in 0.21–0.90 seconds per provider. The test
made no inference request and edited no workspace file. Exact sanitized
results are in
[browser acceptance](evidence/browser-current-source-acceptance-20261014.json).
This verifies the live page and authenticated APIs, not rendered visual
acceptance or a release package.

## Rendered TestProj browser UI — 2026-10-14

The local browser page at `http://127.0.0.1:60417/ui/` restored its backend and
rendered the TestProj workspace. Its accessibility tree showed the file
browser, workspace configuration, right agent sidebar, conversation history,
selected OpenAI profile, saved-key status, and 20 discovered OpenAI models in
the sidebar model picker. Existing assistant messages showed provider-reported
token counts and measured timings; the composer showed the file-change
approval policy. No key value was exposed, and this read-only inspection
submitted no prompt or process task.

The page surfaced a previously uncertain preview-process operation from the
existing conversation. Its quarantine remains in force, so this check does not
claim a fresh coding turn or resolved process effect. A separate VS Code
window handle became stale on two fresh lookups; no interaction was attempted
from it. This is rendered browser evidence only, not screenshot-based visual
review or new browser/VS Code coding acceptance. [Sanitized observation](evidence/testproj-rendered-browser-ui-20261014.json).

## Current browser and VS Code client contracts — 2026-10-14

The current checkout passes the complete automated browser and VS Code
extension contract command **456/456**, with zero failures, cancellations or
skips in 15.42 seconds. This includes workspace switching, provider/model
selection, reconnect ownership, session history, run activity and sidebar
behavior. These contracts do not substitute for the separately recorded live
browser acceptance or prove a current installed VS Code package. [Test result](evidence/browser-vscode-contracts-current-20261014.json).

## Concurrent sibling nested plans — 2026-10-14

Independent dynamic-agent siblings can now accept nested plans while sharing
one root budget. Plan acceptance reads the current budget inside its serialized
SQLite transaction and atomically enforces root-wide child and model-call
limits; a sibling's intervening budget-counter update no longer rejects an
otherwise valid plan. Each owner's plan revision, signed call identity, and
root-wide capacity checks remain enforced.

The synthetic-provider AgentService contract synchronizes both planner
responses and both nested reader calls to prove actual overlap, then verifies
both owner plans, reader histories, terminal states, and settled shared-budget
counters. The five focused dynamic-plan, repository, planning HTTP, engine,
and MCP contracts passed **5/5**. A full Release rebuild succeeded and the
complete native CTest inventory passed **144/144** in 204.76 seconds with
six-way parallelism. This is synthetic-provider contract coverage, not live
provider interoperability.

## Rebuilt native Release gate — 2026-10-14

The current checkout was rebuilt with
`cmake --build build/native --config Release --parallel 4`, then the complete
native CTest inventory passed **144/144** with four-way parallelism in 208.09
seconds using
`ctest --test-dir build/native -C Release -j 4 --output-on-failure`. Six tests
that failed against stale binaries passed after the rebuild, including both
nested dynamic-plan contracts, graph HTTP, patch HTTP, and local-view browser
contracts. This is contract coverage with synthetic provider/peer fixtures;
it does not establish live-provider or installed-client acceptance.

## Nested dynamic-plan observation and shared UI — 2026-10-14

The root plan endpoint now enumerates durable plan-owner relations and returns
descendant run state, topology, accepted revisions, and signed call
receipts with available held-response usage. Root-scoped lookup remains bound
to the depth-zero root owner even when child plans share its `root_run_id`.
Plan and call revisions are rechecked before the HTTP snapshot is returned.
The strict shared client binds each nested owner to the exact admitted parent
agent node, and the shared browser/VS Code renderer displays the nested plan
recursively under its exact admitting node through the supported depth of
eight.

Release native repository and planning HTTP contracts passed **2/2**, including
the root/child ownership tree and actual nested-plan HTTP projection. The VS
Code/shared renderer suite passed **346/346** and the browser suite passed
**109/109**. Follow-up renderer coverage also verifies owner and descendant
transcripts with their per-response metrics. Client and renderer fixtures are
synthetic; the HTTP contract uses actual xlang3-backed persistence with
synthetic provider receipts. The native CLI prints the same complete plan
observation as formatted JSON through its existing `inspect-plan` route.
Nested pause/restart recovery, depth/total-work exhaustion, and rendering
nested sibling plans in all clients remain incomplete.

## Nested dynamic-agent planning runtime — 2026-10-14

Schema v27 extends owner admission to descendant plans through depth eight.
An admitted dynamic-agent child can own and execute nested plans under the
same root budget and native child pool. Owner-scoped tool calls,
continuations and child histories remain bound to the exact claimed parent
node. Repository completion refuses an unfinished child plan and marks a
finished child plan complete in the same transaction as its run. A child yields
its active leaf slot while scheduling descendants, preserving root-wide
parallelism for actual leaf work. A nested human node is rejected with
`nested_human_wait_unsupported`; planning beyond depth eight is rejected with
`planning_depth_exhausted`.

The Release build completed and the focused dynamic-plan contracts passed
**3/3**: repository ownership/lifecycle, nested-plan HTTP projection, and an
actual AgentService provider-peer run from root agent through two planning
owners to a depth-two workspace reader. The engine test also exercises
nested-human rejection and shared root-budget accounting. The extension suite
passed **346/346**; the dynamic MCP and both A2A graph-task integration contracts
passed serially after a full parallel run exposed test-fixture assumptions and
a paused-graph plan-lookup bug. Provider responses and credentials are
synthetic. A follow-up repository contract now admits nine plans at depths 0
through 8, validates their exact owner chain and shared budget, and passes with
the maximum eight child owners. A new repository contract also rejects nested
plans that would exceed root-wide node, revision or human-question allowances.
A ninth owner cannot be created through valid admission because every nested
level consumes a child from the same eight-child root budget; the executor's
depth-nine defense is therefore not directly end-to-end reachable. Remaining
total-work bounds, nested pause/restart recovery,
live-provider interoperability and external-client acceptance remain open.

## Nested plan owner repository admission — 2026-10-14

Repository acceptance now admits a signed plan for a running dynamic-agent
child only when it re-derives that child's active claimed node from the same
ordinary root. The nested plan stores its immutable parent plan/node/depth,
uses the exact finished leaf-model receipt, and adds child/continuation holds to
the shared root counters. The contract verifies valid admission, mismatched-root
rejection, owner lookup, root-wide signed-call enumeration, and aggregate
reservation totals through real xlang3-backed SQLite.

The focused Release build completed and the dynamic-plan repository, HTTP
planning, and engine contracts passed **3/3**. The repository contract
also drives a nested human node through report creation, owner-bound
conversation commit, and a leaf-role continuation reservation against the
shared root budget. Dynamic Agent children receive owner-scoped planning tools;
nested pause/restart recovery was incomplete at this checkpoint. It is covered by
the later “Nested human pause/resume and restart recovery” contract above. Assistant/model receipts and human
input in this contract are synthetic fixtures; it does not claim live provider
execution.

## Owner-scoped signed dynamic-plan calls — 2026-10-14

Schema v26 adds `owner_run_id` to each durable signed planning call and
backfills it from immutable plan ownership. Provider-call idempotency and the
single-pending-call constraint now use the plan owner, while reservations,
limits and consumed budget remain charged to the shared root. Signed-call,
result, conversation-commit and continuation triggers bind their receipts to
that owner. Existing plans and call/event identities survive a transactional
SQLite relation rebuild with foreign-key validation. Legacy handoff snapshots
continue to validate through this additive schema change.

The populated v23 migration contract now checks the backfilled owner and the
owner-scoped indexes. The migration-sensitive native contracts passed **9/9**;
the full Release build completed and the full native CTest inventory passed
**144/144** in 209.21 seconds. The retained CTest log and machine-readable
result are [`native-full-release-ctest-v26-20261014.log`](evidence/native-full-release-ctest-v26-20261014.log)
and [`native-full-release-ctest-v26-20261014.json`](evidence/native-full-release-ctest-v26-20261014.json).
These contracts use local/synthetic peers and providers. Nested planning is
still not admitted by the runtime; see the [recursion contract](native-dynamic-plan-recursion.md).

## Full native Release gate — 2026-10-14

A fresh Release build in the dedicated `build/native-verify` tree completed,
then the complete **144/144 native CTest contracts passed** in 211.38 seconds
with four-way parallelism and no failures or skips. This includes the current
A2A streaming/reconnect, dynamic-plan MCP, graph, CLI, xlang3/SQLite, provider,
and managed local-view contracts. The Rust Analyzer contract also passed in
this full run. The exact CTest output, with secret-shaped strings redacted, is
[`native-full-release-ctest-20261014.log`](evidence/native-full-release-ctest-20261014.log).
The command, return codes, test counts and log digest are recorded in the
[machine-readable result](evidence/native-full-release-ctest-20261014.json).
This is native contract coverage; synthetic peers/providers and local test
fixtures do not establish live-provider or installed-client acceptance.
The current shared browser and VS Code suites also pass **452/452** on the
same source checkpoint; their exact output and command are recorded in
[`browser-vscode-contracts-20261014.json`](evidence/browser-vscode-contracts-20261014.json).

## Automatic outbound A2A stream reconnect — 2026-10-14

The shared native A2A tool registry now handles a bounded stream transport
failure after task identity and an explicit event cursor are durably recorded.
It resumes the same remote task from the latest committed cursor, at most twice
within the existing run deadline, and never sends the task a second time. If
there is no durable cursor, cancellation/deadline occurs, or reconnect fails,
the operation remains uncertain for inspection.

The Release build passed. Focused CTest passed **3/3** for the A2A client,
A2A tool registry and actual dynamic-plan integration. The integration peer
intentionally disconnects after event 1, observes one `SendStreamingMessage`
and an automatic `SubscribeToTask` with `Last-Event-ID: event 1`, returns the
terminal event 2, then verifies an explicit SQLite-reopen subscription starts
at event 2 and does not resend. A second peer case sends the task ID without
an SSE cursor and disconnects; the operation and child remain uncertain, the
plan halts for inspection, and no subscription or second send occurs. The
complete log is
[`native-a2a-auto-resubscribe-focused-ctest-20261014.log`](evidence/native-a2a-auto-resubscribe-focused-ctest-20261014.log).
Provider/peer responses are synthetic. This validates the registered native
tool path, not live external-agent interoperability or recovery when the
remote task ID was never observed.

## Dynamic-plan MCP resource and prompt reads — 2026-10-14

Dynamic-plan schemas now offer registration-bound MCP resource and prompt
aliases from the coding preset. At execution, each model-free child opens a
fresh MCP subprocess, compares its full rediscovered binding with the sealed
catalog, and obtains the usual controller approval before reading. Resource
URIs and prompt arguments are validated by the exact captured schemas; reads
do not require effect-only repository guidance, but still use one durable
operation record and return the peer's actual response to the owned child.

The synthetic peer contract runs a resource read followed by a success-dependent
prompt retrieval through actual xMind AgentService, subprocess clients,
approval journal, and embedded xlang3 SQLite. It verifies the exact resource
URI and prompt arguments, actual returned bytes, dependency ordering, separate
child receipts, and zero effect-tool calls. The focused Release CTest set
passes **6/6** including dynamic-plan schema/repository/engine, the expanded
MCP contract and A2A client/registry. The freshly rebuilt full native Release
inventory completed **143/144** in 779.88 seconds; its sole failure was the
Rust Analyzer call-site-definition contract. An immediate isolated rerun,
including the fixture check, passed **2/2** in 21.57 seconds. `node --check`
and `git diff --check` pass. Live MCP interoperability and rendered/installed
clients remain outside this synthetic peer result.

## Dynamic-child A2A resubscription after SQLite reopen — 2026-10-14

An approved A2A node in a dynamic plan now has an end-to-end restart contract
for the existing explicit resubscription route. After the child run stores two
stream events and cursor `dynamic-a2a-event-2`, the fixture closes the original
AgentService and xlang3-backed SQLite repository, reopens the database, and
resubscribes through the authenticated native HTTP route. The synthetic peer
receives the saved cursor and returns event 3. The contract verifies event 3
is appended to the original child run, the completed plan remains unchanged,
and the audit contains one initial send and one resubscription. At that
checkpoint this proved explicit recovery for a dynamic child-owned operation,
not automatic recovery. The later bounded automatic reconnect is covered in
the current section above. Live external-peer interoperability remains
unverified.

The focused Release CTest set passes **6/6** across the A2A client and registry
contracts plus dynamic-plan schema, repository, engine, and MCP/process/A2A
contracts. `node --check` and `git diff --check` also pass. The full native
inventory last passed **144/144** before this checkpoint and was not rerun.
Recursive planning, live external providers/peers and rendered/installed
planner acceptance remain open.

## Full native Release suite — 2026-10-14

A clean Visual Studio 18 Release build in `build/native-verify` passed the full
native CTest inventory **144/144**. This includes the three optional Rust
Analyzer and multi-folder LSP contracts, configured explicitly for the fresh
build. The separate build tree was necessary because the TestProj server the
user is testing still had the regular `build/native/contract-bin/Release/xmind.exe`
open. A first run against those stale in-place binaries produced four stale
contract mismatches and one transient local-view fixture failure; all five
passed with freshly built binaries, and the full clean run passed. The
provider CLI expectation now includes the already-supported `mistral.chat`
route. This is the native Release gate, not hosted CI, live-provider acceptance
or installed-client acceptance.

## Direct MCP nodes in dynamic plans — 2026-10-14

Release builds of `xmind` and the dynamic-plan contracts pass. Focused CTest
passes **2/2** for the native dynamic-plan engine and MCP contracts. The MCP
contract now includes a model-selected direct MCP `tool` node: the actual
peer is rediscovered from the frozen server revision, the existing controller
approval is required before dispatch, one operation outcome is journaled, and
a dependent native read observes the actual written bytes. Synthetic provider
replies and credentials are used; the MCP subprocess, native approval/effect
path and embedded-xlang3 SQLite are actual. The full native Release CTest
inventory completed with **143/144** passing; the sole failure was the
Rust Analyzer call-site-definition contract. An immediate isolated rerun,
including its Cargo fixture check, passed **2/2**, so the integration failure
did not reproduce. The dynamic-plan/MCP contract passed in the full run. The
VS Code suite passes **343/343**, including the 87
renderer contracts for exact tool arguments and an explicit “controller
approval required” MCP label. Browser unit/controller tests pass **109/109**;
the native browser harness requires its server/assets arguments and is not
covered by that unit-test count.
Browser/VS Code installed acceptance, live external providers/peers, background
jobs inside plans and recursive planning remain unverified or incomplete.

## Synchronous process nodes in dynamic plans — 2026-10-14

Dynamic `tool` nodes can now select the registered `run_process` profile from
the frozen coding preset. Native C++ validates against the sealed process
schema, restricts execution to the primary workspace folder, rejects
background mode, and dispatches through `ProcessExecutor` so the usual exact
approval, operation ledger, process output artifact and uncertain-outcome
behavior remain authoritative. The process instruction precondition is scoped
to its selected workdir. A focused fixture approved a real Node process in a
nested workspace directory, verified its actual file write with a dependent
native read, and separately denied a launch while asserting no output or file
effect.

The dynamic-plan pure, repository, engine, and MCP/process/A2A contracts pass
**4/4**. The clean full native Release suite recorded above passed **144/144**
after the earlier 143/144 run's Rust Analyzer failure passed its isolated
rerun. The full suite has not been rerun for the process/A2A additions. The VS
Code and browser results above are from the preceding MCP checkpoint; these
backend changes did not modify either client. Background jobs inside plans,
rendered/installed acceptance and recursive planning remain open.

## Registered A2A nodes in dynamic plans — 2026-10-14

Dynamic `tool` nodes can now select an exact registration-pinned
`a2a_delegate_*` binding from the frozen coding preset. The child performs a
fresh AgentCard discovery and rejects any registration/card drift before it
offers the usual native controller approval. After approval, the registered
A2A client sends once, stores the actual task ID/result and appends both real
streamed task events plus their latest explicit cursor to the child run;
denial completes the plan report without sending. The contract uses actual
native AgentService, registration, operation approval/effect persistence and
embedded xlang3 SQLite against a synthetic local A2A 1.0 streaming peer and
synthetic provider response. It does not claim live external-peer
interoperability.

The focused dynamic-plan Release CTest set passes **4/4**, including both
approved and denied direct A2A scenarios, MCP/process effects, pure schema
admission and repository/engine contracts. The later child-owned explicit
resubscription restart contract is recorded above. Recursive planning, live
external providers/peers and rendered/installed planner acceptance remain
open.

## Dynamic deterministic workspace-tool nodes — 2026-10-14

The working tree adds an explicit dynamic-plan `tool` node for six fixed
read-only workspace aliases. Native C++ validates the exact argument JSON
against the sealed tool schema and frozen inspect capability, executes under
the admitted workspace as a model-free owned child, and verifies ordered
start/completion receipts during settlement and recovery. SQLite schema v23
stores node-kind-specific definitions; schema v24 adds immutable plan-owner
scope, and schema v25 removes one-plan-per-root uniqueness. Agent and human
definition decoding remains compatible with earlier saved rows.

The pure dynamic-plan, repository and actual native engine contracts pass on
this working tree. The engine fixture executes `read_file` after a dependent
coding child and checks the real returned bytes, separate child ownership and
model-free budget accounting. The shared Browser/VS Code client now validates
tool definitions and child claims, and the shared renderer displays exact
arguments and actual model-free output. The full VS Code suite passes 343/343;
provider responses and keys remain synthetic. Hosted CI, live-provider use,
rendered external-browser UI and installed VS Code were not run for this
checkpoint. MCP/process plan nodes and recursive planning were incomplete at
that checkpoint; direct MCP and synchronous process nodes are covered in the
current sections above. Recursive planning remains incomplete.

## Empty provider admission and unified browser acceptance — 2026-10-14

The browser and VS Code clients now omit provider-profile admission when no
profile is active. This keeps an empty provider ID out of the native admission
request, allowing a model-free graph to run; configured profiles still use the
existing revision-bound admission. The browser-native test harness now invokes
the unified `xmind.exe serve` entry point and asserts both the loaded graph and
the admitted run identity.

Commit `b238fdd` passed all **341 VS Code extension tests** and the actual
unified native/browser contract. That contract verified local view-session
authentication and revocation, a human-paused graph, controller input, a real
workspace file read, gateway disconnect/reconnect, and durable history without
replaying the run. The managed local-profile lifecycle contract also passed:
owner verification, graph pause/resume, blocked and successful runtime
replacement, stable owner port, and history after restart. It made **zero
provider requests** and explicitly did not claim installed-IDE acceptance.

An isolated executable initially failed during SQLite startup because its
paired `xlang3_runtime.dll` had not been staged beside it. Staging the matching
DLL and rebuilding the test's browser assets resolved the harness failures.
The xlang3 `isolation_level` and Python SQLite API tests plus the embedded
xlang3 SQLite contract pass. No xlang3 source change was required or made.
This acceptance used synthetic/model-free flows; no provider inference ran.

The rendered external browser and VS Code host remain unverified in this
checkpoint. The computer-use controller returned `GetCursorPos failed: Access
is denied` when launching Edge and Chrome; VS Code window activation failed
even after refreshing the returned window list. Exact commands, result logs,
binary/module hashes and these limits are in the
[checkpoint evidence](evidence/checkpoint-b238fdd-20261014.json).

## Durable A2A SSE resume cursor — 2026-10-13

Resubscription now parses bounded SSE `id` fields and journals a non-empty
peer cursor together with its task event. Before a later manual resubscription,
the backend reads the latest cursor for that exact operation from the xlang3
SQLite-backed run journal and sends it as `Last-Event-ID`. The route returns
the cursor it most recently committed. Cursors are scoped to the recorded run
and operation; malformed or oversized values are rejected before becoming an
HTTP header. The native client and A2A registry contracts passed **2/2** in
Release, including an actual two-subscription peer exchange and durable cursor
recovery through the repository. A2A peers may omit or ignore SSE IDs, so this
supports resumable peers without claiming protocol-wide exactly-once delivery.
After backend reopen, the contract resumes an uncertain task using its saved
cursor and appends the observation to the terminal run journal without
replaying the initial send. Terminal-run appends are limited to task-matching
observations owned by a succeeded or uncertain A2A operation. A mismatched
remote task is rejected. The client, registry and SQLite repository Release
contracts pass **3/3**; see the [restart CTest log](evidence/native-a2a-sse-restart-ctest-20261014.log).

## Durable A2A peer-event deduplication — 2026-10-14

Initial streamed sends and manual resubscriptions now append through one
serialized xlang3/SQLite operation. For an A2A operation, an explicit peer SSE
ID can be journaled once; a replay with identical task-event content is
ignored, while reuse of that cursor for different content is rejected. A
replayed older event does not move the saved `Last-Event-ID` backwards.
`A2aStreamEvent.event_id` identifies only an ID explicitly present on that
frame, rather than inheriting a prior frame's cursor and accidentally
deduplicating a distinct frame without an `id` field. The Release A2A client
and agent-registry contracts pass **2/2**, covering explicit versus inherited
frame IDs, replay from a resubscription, cursor retention and cursor/content
conflict. Events without an explicit SSE ID remain at-least-once observations;
this does not establish exactly-once delivery across peers or automatic stream
recovery.

## Explicit outbound A2A task resubscription — 2026-10-13

The owner API and shared Browser/VS Code operation panel can explicitly
resubscribe to a saved remote A2A task after its original streaming send is
interrupted. Resubscription pins the saved peer revision and task identity,
never repeats the original send, accepts at most 64 events/512 KiB, and appends
each received event to the original run's durable SQLite journal. The peer
stream has a 20-second bound; already committed events remain visible if the
request times out. Where the peer supplies SSE IDs, xMind retains and sends
the last journaled cursor as `Last-Event-ID`; peers without cursor support may
replay events. Terminal tasks or peers without streaming return an error; the
read-only task inspection path remains available.

Release build of `agentflow_a2a_agent_registry_contract` passed, and its CTest
passed **1/1**. The full browser suite passes **109/109** and the full VS Code
suite passes **340/340**. These contracts verify the endpoint and rendering
messages, not rendered/installed browser/VS Code acceptance or
automatic stream recovery after backend restart. See
[outbound A2A limits](native-a2a-client.md).

## Rendered TestProj Saved Models acceptance — 2026-10-13

The live Model Zoo page at `http://127.0.0.1:5173/` passed a browser-level
Saved Models check. Saving BERT on catalog page 1 changed its accessible
control to a checked “Unsave” state; enabling Saved models wrote `?saved=1`,
showed one result and survived reload. After removing that temporary save, a
model saved from catalog page 2 (EfficientNet-B0) appeared as the sole result
on the Saved view's page 1 and remained selected after reload. This exercises
the actual rendered Vue page and its localStorage/query behavior, beyond the
successful Vite build and source contracts.

Both temporary saved IDs were removed through the UI; the filter was disabled
and the page returned to `/`. Search/facet combinations beyond this cross-page
case and broader xMind/OpenCode parity remain open. See [live browser evidence](evidence/testproj-saved-models-live-20261013.json).

## Durable session prompt inbox — 2026-10-13

The browser and VS Code sidebar can now queue prompt input during an active
session run. The interactive CLI uses the same durable inbox and automatically
admits queued prompts in order after a turn ends. SQLite retains pending input
across page/backend reconnects, the sidebar shows queued text, and run creation
and claiming the inbox item are atomic. The admitted prompt must exactly match
the queued content. This is part of xMind's backend/UI path; it required no
general xlang3 optimization.

Queued items can now be canceled from the browser and VS Code. Native
cancellation is transactional and idempotent; canceled IDs remain reserved in
SQLite while canceled rows stay hidden from inbox listings. The v19→v20
migration contract verifies queued and claimed items, including the claimed
run link, survive upgrade. Release `xlang_repository_contract` and
`native_agent_http_contract` pass **2/2**; browser and VS Code contracts pass
**98/98**.

The real native HTTP/CLI contract now also verifies queued prompt admission,
exactly-once history and the claimed run link. It passed on Release; the browser
classic-script and HTTP-adapter suites previously passed **14/14**. See
[durable inbox behavior and limits](native-session-inbox.md), [CLI test log](evidence/native-cli-inbox-ctest-20261013.log),
and checkpoint `75a1d54`.

## Current xMind AgentRunner xlang3 process acceptance — 2026-10-13

The current unified `xmind.exe` backend loaded the saved local provider
profiles, discovered the DeepSeek catalogue without asking for a new key, and
ran a provider-backed TestProj request through the native `run_process` tool.
The persisted process record independently confirms profile `xlang3`, the
paired xlang3 executable, the automatically supplied runtime `modules` and
workspace `.xlang3-site` import roots, process exit code **0**, and a retired
process tree. Pytest reported **9 passed, 1 warning in 1.83s**. `-B` and
`-p no:cacheprovider` prevented bytecode and pytest-cache writes; the post-run
file-time scan found no recently modified backend files. TestProj is not a Git
repository, so this check cannot produce a Git diff.

The isolated browser adapter is live at `http://127.0.0.1:51935/ui/` and is
bound to that same backend and workspace. Its local enrollment uses the
backend configuration directly; the UI does not need a server-token prompt.
This is backend and API evidence, not rendered external-browser acceptance:
Chrome/Edge control was unavailable in this environment. The earlier page at
60420 was launched from an older staged xMind runtime, which lacked the paired
xlang3 profile; no xlang3 source change was needed for the current run.

The exact executable identities, operation record and provider usage counters are in
[provider-backed run evidence](evidence/testproj-xmind-agent-xlang3-web-live-20261013.json).

## Bounded native and rendered session history — 2026-10-13

The native server now exposes SQL-bounded `GET /v1/sessions/{id}/messages`
pages, and the console CLI exposes `history-page SESSION LIMIT
[BEFORE_MESSAGE_ID]`. Pages return the latest messages in chronological order;
each `next_before` boundary continues to the preceding page. Limits are capped
at 1,000, and cursors are checked against the requested session. The shared
client validates response shape and cursor consistency, and the local browser
adapter allows both authenticated page reads and exact single-message reads.
The shared native/browser client validates the single-record identity and
payload. Both rendered chat views retrieve the latest 100 messages, merge
refreshed snapshots without duplicate records, and request older pages on
demand. Prepending a page preserves the reader's scroll position. Existing
full-history clients remain supported.

Release `native_http_cli_contract` passed **1/1** (3.63 seconds), covering
three pages of a twelve-message transcript, continuation without skipped or
repeated messages, HTTP/CLI equivalence, limits, malformed and missing
cursors, unknown/duplicate query parameters and single-message lookup.
The current VS Code suite passes **334/334**, and the browser suite passes
**107/107**. The native browser contract also retrieves a persisted message by
ID through the HttpOnly cookie after reconnect. Full OpenCode message/part
projection, message-type filters, selectable ordering and opaque cursors remain
incomplete.
See
[bounded-history scope](native-session-history-pages.md) and
[local evidence](evidence/native-session-history-pages-20261013.json).

## Native provider history, MCP SDK and Release validation — 2026-10-13

The full Release build succeeded in `build/native`, which uses an isolated
contract output directory. Anthropic, Gemini and DeepSeek history receipts are
validated before canonical text/tool projection onto compatible foreign wires.
Responses receipts are retained only for Responses replay; switching to Chat
fails with the durable `incompatible_provider_history` reason. A synthetic
Claude-to-Gemini tool-history transport remains covered. The official MCP SDK
modern and legacy tool peers pass their actual approval, denial, cancellation
and uncertainty flows; native resource-catalog tests run against the separate
native peer that implements those methods.

The first full 144-test run exposed three failures: the Responses-to-Chat
fail-closed contract and two SDK tests that mistakenly ran native-only resource
modes against SDK tool-only peers. Those issues were corrected; the four focused
regression contracts passed, then the complete post-fix Release suite passed
**144/144 in 676.83 seconds**, with zero failures or skips. The full transcript
is [native-144-ctest-postfix-20261013.log](evidence/native-144-ctest-postfix-20261013.log).
The earlier failures and their focused retest are retained at
[native-144-ctest-20261013.log](evidence/native-144-ctest-20261013.log).

## xMind development-host runtime selection — 2026-10-13

When `Tools/start-ui.ps1` omits `-RuntimeDirectory`, it now reads the runtime
paired with `build/native` from `CMakeCache.txt`, then verifies the xMind and
xlang3 runtime DLL hashes before snapshotting the interpreter. A four-case
PowerShell contract covers configured-cache selection, explicit override,
missing paired executable and fresh-checkout fallback. A fresh VS Code
development host opened TestProj using the CMake-paired
`build-pytest-pathaware/Release` runtime; the staged executable and DLL hashes
matched their sources, and xlang3 started successfully. That VS Code preview
still has no active model. A separate current unified backend was configured
from the saved provider YAML and completed the agent-issued TestProj pytest
run using the paired xlang3 process profile; this does not establish rendered
VS Code or external-browser acceptance.
[Acceptance record](evidence/start-ui-xlang3-runtime-pair-20261013.json).

Fresh native CMake configurations now prefer the paired
`build-pytest-pathaware/Release` xlang3 runtime when both its Windows DLL and
import library exist, and fall back to `build/Release` otherwise. A clean
configure smoke selected the paired directory. After reconfiguring the focused
Release build against it, the actual AgentService, AgentRunner and backend-owner
HTTP contracts passed **3/3**. Browser and VS Code client suites passed
**107/107** and **334/334**. This fixes a stale-build false failure and keeps
xlang3 work scoped to runtime compatibility needed by xMind.

## Packaged xlang3 plugin integration — 2026-10-13

xMind main `9caef7c` now pins xlang3 compatibility branch commit `f6bbc56` and
stages the selected xlang3 plugins required by xMind and its Python dependency
path. The accepted plugin inventory contains 20 `.x3pkg.dll` files and three
pure Pydantic facade sources; probe and sample packages, `.pyd`, bytecode and
linker artifacts are excluded. A local package smoke staged 1,921 files from
the actual xMind and xlang3 Release binaries plus pure Python 3.14 library
sources. The compiled Windows runtime-generation verifier accepted the exact
staged manifest (`d5577048…aebeef2`).

The packaged xlang3 executable then ran the real Model Zoo backend test suite
from TestProj with its installed pure Python dependencies: **9 passed**, with
one AnyIO deprecation warning. A packaged xMind server also reached its local
listening state from this runtime. The test used xlang3 with bytecode writing
disabled; it did not execute CPython or contact a provider. The hosted view
and extension gate now passes on the updated source; the separate native
Windows package/contract gate remains pending. Exact local package and test
results are in the [smoke record](evidence/xlang3-runtime-module-package-20261013.json).

The paired xlang3 process path now adds `-B` for every xlang3 profile so native
bytecode cannot mutate the verified runtime package. Its Release process
contract passed, the unified xMind executable rebuilt, and the development UI
snapshot recipe staged the matching runtime DLL, `xlang3.exe`, 20 product
plugin DLLs and 3 pure facades. Running TestProj's real backend suite from that
snapshot again passed **9/9** without creating `__pycache__` in the runtime.
This validates the snapshot contents and paired interpreter; it does not
restart or claim rendered acceptance from the currently open browser run.

## View-contract gate test inventory — 2026-10-13

The hosted browser/VS Code contract job on `9caef7c` ran its suites but failed
the aggregate gate because `Tools/ci-views.mjs` still expected 47 tracked
view files, 276 extension tests and 81 browser tests. The current source
inventory is 57 tracked files (59 including its two pinned vendor files), and
the actual complete suites pass **328/328** extension tests and **105/105**
browser tests on this machine. The gate's exact inventory and pass-count
expectations now match those measured results. The guarded aggregate gate
passed on `70bc353` with 328/328 extension and 105/105 browser contracts and
its source/asset immutability check; see the [hosted run](https://github.com/xlang-foundation/xMind/actions/runs/38458592874).
The separate native Windows package/contract gate has not passed yet.

## Current VS Code paging fixtures and hosted view gate — 2026-10-13

The extension now uses SQL-bounded session message pages, but its synthetic
host transport still served only the legacy full-history route. The fixture
now returns the paged response shape for saved and forked sessions, and the
route assertions track the current API. The complete VS Code suite passes
**332/332** and the browser suite passes **106/106**. The guarded view workflow
now enforces these current counts instead of stale 328/105 values. The native
server and xlang3 were not changed by this fix.

## Installed VS Code extension attaches to the xlang3 branch owner — 2026-10-13

VS Code 1.141.0 loaded installed xMind extension 0.1.10 in a trusted TestProj
window. The `agentflow.open` command connected the sidebar without a displayed
extension error. It attached to the pre-existing TestProj owner; the owner's
xlang3 executable, runtime DLL, and native JSON/SQLite modules match the
bundled compatibility-branch package. This verifies the installed client-to-
shared-owner attachment, but not process startup from this host or a
model-generated edit. [Run evidence](evidence/native-vscode-packaged-owner-attachment-20261013.json).

## Packaged runtime owner startup with xlang3 compatibility branch — 2026-10-13

The packaged `xmind.exe` started a fresh local TestProj profile with
`started=true`, created its SQLite database, and served the CLI `sessions`
request from that profile. The managed runtime copy's `xlang3.exe` SHA-256
matched the VSIX-bundled executable. The isolated, idle owner was stopped after
the check. This directly verifies package-to-native-owner startup with xlang3
branch `fix/pytest-pathaware-hook-proxy` at `f6bbc56`; it does not include a
model request or workspace write, and the installed VS Code host's attribution
to this owner remains unverified. The package binaries came from xMind source
`7411150`; current xMind main `37d862f` adds the validation record.
[Run evidence](evidence/native-packaged-runtime-owner-smoke-20261013.json).

## Live webpage provider and coding acceptance — 2026-10-13

The local webpage loaded the saved Claude, Gemini, DeepSeek and OpenAI profiles
from `.config/providers.yaml` during server startup; the browser exposed provider
metadata only, with credential values redacted. The OpenAI account discovery
returned 20 models, and the composer selected `gpt-4.1` without a second key
prompt. A read-only TestProj request correctly identified the Model Zoo frontend
as `model-zoo-frontend`, Vue 3, with `vite build`; the response displayed
provider-reported token counts (2,945 input and 72 output).

The subsequent webpage coding run redesigned the catalog in
`TestProj/model-zoo/frontend/src/views/HomeView.vue`. The first build found a
missing Vue section close; the agent repaired it, and live review then exposed
duplicated legacy hero/disclosure blocks. A follow-up removed the duplicates.
The final `npm.cmd run build` passed (37 Vite modules, 607 ms). A fresh live
browser render showed one filter rail, hero, data disclosure, results row, grid
and pager. Selecting the Text Generation task returned GPT-2 on page 1 of 1;
Reset restored the 12-model catalog on page 1 of 2. This verifies one real
provider-backed webpage edit/repair/build/filter cycle, not full coding parity.
Sanitized run and UI evidence: [browser-live-modelzoo-coding-20261013.json](evidence/browser-live-modelzoo-coding-20261013.json).

## Shared native CLI and VS Code profile — 2026-10-13

The exact xMind main source at `7411150` and xlang3 compatibility branch at
`f6bbc56` passed the complete local native gate (**141/141 CTest**), VS Code
extension suite (**324/324**), browser suite (**103/103**) and browser/native
integration. A fresh bridge-disabled runtime package verified **1,900** files,
including six native inputs and 1,869 pure standard-library sources; VSIX
`0.1.10` packaged and installed in VS Code `1.141.0`. Opening TestProj caused
an isolated real VS Code extension host to load the installed package at its
exact version, execute **xMind: Open Workspace** against the trusted TestProj
folder and report no extension error. The host did not spawn a separate native
process, so backend attribution remains unverified. This is local Windows
package and extension-host evidence, not a hosted release gate, rendered
sidebar capture or live model run. Exact hashes and scopes are in the
[local package evidence](evidence/native-local-package-20261013.json); its
[source inventory](evidence/native-local-source-manifest-20261013.json) binds
the staged native files, SDK modules and license notices.

The native CLI now uses the same default profile root as the VS Code extension:
`%USERPROFILE%\.xMind\p`, falling back to `%USERPROFILE%\p` when the
primary path exceeds the native runtime's 84-character limit. A Release CLI
`profile-info` attached to the already-running TestProj owner (`started: false`,
PID 44084), then `sessions` listed the existing Model Zoo and VS Code sessions.
This confirms both clients reach the same managed backend and SQLite history.
The full `native_local_profile_contract` passed **1/1** in **212.07 seconds**,
covering managed startup, encrypted rendezvous, concurrent attach,
profile-preserving upgrade/recovery and persisted graph history. Exact CTest
output: [native-cli-vscode-shared-profile-20261013.log](evidence/native-cli-vscode-shared-profile-20261013.log).

## Live native CLI agent turn — 2026-10-13

From `xmind chat` on that same TestProj profile, Claude Sonnet 4.6 read
`model-zoo/frontend/package.json` through the native `read_file` tool and
reported the package name and Vue 3 framework. The run completed with CLI exit
status **0**, returned provider-reported usage for both model calls, and made
no workspace changes. This verifies one live CLI provider/tool cycle, not
approval-based writes, terminal execution or full interactive CLI parity.
[Sanitized run metadata](evidence/cli-live-modelzoo-20261013.json).

## Native CLI workspace process build — 2026-10-13

From the same live TestProj CLI session, Claude invoked the managed
`run_process` tool in `model-zoo/frontend` with `npm.cmd run build`. Vite 6.4.4
transformed 37 modules, emitted the production bundle to `dist/`, and exited
**0** in approximately **593 ms**, with no reported warnings. The agent turn
and CLI both completed successfully. This verifies a live model-to-process
tool cycle on the shared backend; it does not establish interactive approval
coverage or broad shell compatibility.
[Sanitized run metadata](evidence/cli-live-process-build-20261013.json).

## Native CLI approved file write — 2026-10-13

Claude Sonnet 4.6 issued one native `create_file` proposal in the same TestProj
CLI workspace. The CLI displayed the exact operation and accepted `/allow` for
that operation ID; the run completed with exit status **0**. I independently
verified the created file's 55 bytes and SHA-256, then removed the temporary
acceptance file. This confirms one live model-to-file `create_file` approval
from the CLI; separate live `replace_file` and `patch_file` approvals are
documented below. [Sanitized run metadata](evidence/cli-live-approved-file-write-20261013.json).

## Live configured-provider CLI edit preview and approval — 2026-10-13

Using the saved local provider configuration, the native CLI discovered and
selected Claude Sonnet 4.6 without asking for the API key again. In an isolated
temporary workspace, the live model proposed one `replace_file` operation. The
CLI displayed the verified before/after preview; approving that operation ID
changed only `message.txt` from `before` to `after`, and the run completed.
The temporary server and workspace were removed. This verifies the live-model
CLI edit path; full coding parity remains open. Live `patch_file` approval is
documented in the later acceptance below. [Sanitized evidence](evidence/native-cli-live-edit-preview-20261013.json).

## Live configured-provider CLI patch preview and approval — 2026-10-13

The live Claude Sonnet 4.6 run initially exposed a mismatch between standard
numbered unified-diff headers and xMind's custom patch grammar. xMind's patch
tool guidance now explicitly documents bare `@@` or exact-text anchors and
forbids numbered hunk ranges. The rebuilt native CLI then displayed both
changes in its bounded before/after preview. Approving the exact `patch_file`
operation changed only `message.txt` (`Alpha` to `Beta`, `Omega` to `Sigma`);
the middle line and sole-file workspace inventory were verified, and the run
completed. `native_patch_tool_contract` passed 1/1. No xlang3 source change was
needed. [Sanitized evidence](evidence/native-cli-live-patch-preview-20261013.json).

## Interactive CLI file-change previews — 2026-10-13

The unified CLI now follows its machine-readable `operation_review` event with
a concise per-file before/after preview for create, edit and patch operations.
File paths and lines are JSON-escaped before terminal output, and each side is
bounded to 40 lines and 8 KiB. The exact durable operation event remains
available to machine clients, and `/allow`/`/deny` still target the same
operation ID. Focused Release CTests `native_agent_edit_http_contract` and
`native_agent_patch_http_contract` passed 2/2 with actual files and approval
decisions against synthetic inference. Live-model CLI edit acceptance is
documented above; this contract does not establish live `patch_file` or
complete coding parity.

## Narrow browser sidebar layout — 2026-10-13

Live inspection found a cascade-order bug at a **648 px** viewport: the
workspace pane remained visible at 60 px while the saved 648 px agent sidebar
overflowed the page by 60 px. The final mobile rule now hides the workspace
pane and fills the viewport with the chat sidebar. After rebuilding and
restarting the isolated TestProj view, the same saved history reconnected
without another prompt; document width matched the viewport at **648 px**.
The browser controller/renderer suite passes **100/100**, including a
regression check that the mobile rule follows the component defaults it must
override. This verifies the narrow-window layout only; desktop splitter drag
and resizing remain scoped to their existing contract.
[Live layout measurements](evidence/browser-mobile-layout-live-20261013.json).

## Native A2A Windows contract checkpoint — 2026-10-13

The local Release build passed all seven focused A2A CTests on Windows:
native client, official JavaScript SDK peer, agent registry/delegation, and
the task-control, message, streaming, and v1 HTTP contracts. The complete
output is saved at
[native-a2a-focused-ctest-20261013.log](evidence/native-a2a-focused-ctest-20261013.log).
This verifies the focused protocol and local effect contracts; remote task
recovery/resubscription, artifact parity, real remote-model delegation, and
cross-client CLI/VS Code acceptance remain open.

## Claude coding response budget and live VS Code write — 2026-10-13

The native provider gateway now assigns a bounded 16,384-token default to
current Claude coding model families (Sonnet 4.5/4.6 and Opus 4.5–4.8, plus
the newer supported families); explicit application limits still take
precedence, and legacy families retain 4,096. The native profile runtime
contract verifies the Sonnet 4.6 request budget and persisted provider
provenance (**1/1** focused CTest passed).

In the live VS Code Extension Development Host, the saved TestProj workspace
ran Claude Sonnet 4.6, proposed the exact single-line `edit_file` change for
the Model Zoo search landmark, and completed after the normal approval. The
saved run reports 4,823 input and 148 output tokens. The changed Vue project
then passed `npm.cmd run build` (37 modules, 656 ms). This verifies one live
provider-backed coding write and production build; broader installed-package
and coding parity acceptance remain open.

## Managed runtime recovery and VS Code provider import — 2026-10-13

Stopped managed profiles now replace an obsolete runtime directly only after
the protected process ID/birth record proves its owner exited. Recovery keeps
the existing SQLite profile and saved port, then starts the currently verified
runtime; it no longer tries to restart an old server that rejects current
managed launch arguments. Separately, the local view adapter now routes provider
configuration import with its backend credential only when the request uses the
exact trusted provider file selected at adapter startup. Other file paths and
untrusted view-session imports are refused.

The Release `native_local_profile_contract`,
`native_local_view_provider_config_contract`, and `native_local_view_contract`
pass. The focused profile test verifies stable origin and exact sessions,
history, and graph state after dead-owner recovery. The view tests verify
trusted provider import and that view sessions cannot use the import route.
The VS Code suite passes **320/320**. Packaged Dev Host reconnection and a live
provider-profile import remain the next acceptance check.

## Native agent stale-edit recovery — 2026-10-13

The agent now receives explicit recovery guidance when an edit proposal fails
because its file preconditions are stale: no effect was applied, it must reread
the current file, and it must submit a fresh proposal based on those bytes.
`edit_file` documents the same behavior. The native HTTP agent contract exercised
the full sequence with a synthetic model peer and a real temporary workspace:
the stale call left the file unchanged, `read_file` returned the current
contents, a newly approved exact edit succeeded, and the final file contained
only the approved replacement. Release `native_agent_runner_contract` and
`native_agent_edit_http_contract` passed **2/2**. This is backend contract
evidence.

The same race was then exercised in the live webpage with OpenAI `gpt-4.1` on
TestProj. After the first exact edit reached approval, the disposable test file
was changed from `version=baseline` to `version=current`. Approving the stale
proposal produced `content_conflict` with no write. The model reread the actual
file, proposed a fresh edit from `version=current` to `version=recovered`, and
completed after the second normal approval. The run reported no other files
modified; the recovered bytes were verified and the temporary test file was
removed. This acceptance used an isolated verified xMind Release package and
profile. It verifies this webpage recovery path with one real provider/model;
broader conflict cases remain open.

A live VS Code 1.141 Extension Development Host opened TestProj with the saved
DeepSeek provider and reached the normal file-approval UI. Changing the
temporary file during the approval pause produced `content_conflict` and no
write. That host was attached to an older xMind server binary, which treated a
changed value on the same line as ambiguous and asked before overwriting it.
After explicitly authorizing that overwrite for this temporary test only, the
agent reread the file, submitted a fresh `version=concurrent-edit` to
`version=agentflow-written` proposal, and wrote it after normal approval. The
result was verified and the temporary file removed. This verifies VS Code's
live proposal, approval, and conflict guard; live VS Code acceptance against
the current server binary's stale-edit recovery guidance remains open.

## TestProj webpage coding acceptance — 2026-10-13

The local xMind webpage was connected to the saved TestProj workspace and ran
the OpenAI `gpt-4.1` agent against the Model Zoo frontend. The coding run added
accessible save/unsave controls and a Saved Models filter. The first version
filtered only the current API page; live review caught that saved records on
other pages would be omitted. The webpage editor then repeatedly reported a
content conflict while attempting the repair, so the missing HomeView state and
cross-page loading logic were completed directly in TestProj. The xMind webpage
subsequently ran `npm.cmd run build` against that current content: exit **0**,
Vite transformed **37 modules** and built in **591 ms**, with no stderr.

The live Vue page at `http://127.0.0.1:5173/` verified save/unsave pressed
states, persistence after reload, the `saved=1` URL state, composition with
search/task/framework filters, and a model saved from API page 2 appearing in
Saved Models after returning to page 1. Unsave removed records immediately and
the empty state appeared when the final saved record was removed. Test saves
were cleared after the check. This validates one local browser coding workflow
and the Model Zoo test project. The live VS Code development-host write/conflict
acceptance is recorded above. Installed VS Code package and command
acceptance remain open. No backend or model-data files changed.

## xlang3 pytest and mmap compatibility branch — 2026-10-13

The pushed xlang3 candidate [`b3ec9bf4`](https://github.com/CantorAI/xlang3/commit/b3ec9bf4)
is on `fix/pytest-pathaware-hook-proxy`, based on current xlang3 `main`
`f0c425cf` and four commits ahead. It fixes pytest's lazy
`PathAwareHookProxy` keyword lookup and native `memoryview(mmap)` buffer
exports, including derived-view lifetime and close protection. It includes the
Pydantic Core 2.50.1 pure-Python facade and iterative schema-gather bridge
beside xlang3's native validation/serialization core, plus `ast.alias` source
location constructor support for pytest assertion rewriting.

The candidate's full Release CTest suite passed **56/56**, and the full CLI
fixture suite passed against its Release executable. TestProj's Model Zoo
backend passed **9/9** pytest API tests with xlang3, pytest 8.4.2, FastAPI
0.120.4 and Pydantic 2.14.1. The 11-case performance gate passed with 21 paired
samples against the preserved pre-change executable. Earlier isolated xMind
rebuilds passed **137/137 native CTest contracts**, and browser tests passed
**99/99**; the VS Code extension suite passed **320/320**. The branch is pushed
and includes latest main, but it is not merged or installed as xMind's default
runtime. The default remains xlang3 `main` `f0c425cf`. The live candidate
preview is available at `http://127.0.0.1:60543/ui/` while its local backend
remains running. The final 11-case performance gate after AST location support passed with 21
paired samples; the recorded follow-up is in the
[pytest compatibility trial report](https://github.com/CantorAI/xlang3/blob/b3ec9bf4/doc/performance/pytest-pathaware-hook-proxy-trial-20261012.md).

## Live webpage coding flow and Model Zoo design pass — 2026-10-12

From the xMind webpage on the saved TestProj workspace, DeepSeek Flash
inspected the current Vue/FastAPI implementation and proposed one
`patch_file` edit. After reviewing and approving that file-level diff in xMind,
the run changed only `model-zoo/frontend/src/style.css`: catalog cards gained
stronger title/owner hierarchy, aligned summary/stat rows, a focus/hover accent
rule, and tighter small-screen card spacing. The edit also removed a duplicate
`--line` declaration. It preserved the Vue templates, model data, controls,
accessibility markup and illustrative-data disclosure.

The same xMind run invoked its managed PowerShell process tool from
`model-zoo/frontend`; `npm.cmd run build` exited **0**. Vite 6.4.4 transformed
36 modules and built in **700 ms**, with no stderr. The browser page refreshed
with the new card styling. Live interactions still returned three
Image Classification results, Clear all restored 12 models, and search for
`bert` returned two matches. An independent local API request to
`GET /api/models?page_size=2` returned HTTP **200**, two entries and total **12**.
No backend pytest was run, so this does not clear the existing xlang3/pytest
hook-lookup compatibility failure. Phone-sized rendering and the full Vue/API
test suites remain to be exercised. [Sanitized evidence](evidence/testproj-webpage-design-run-20261012.json).

## VS Code workspace and API inspection smoke test — 2026-10-12

In the running VS Code Extension Development Host, the xMind extension used the
opened `D:\\CantorAI2026\\TestProj` workspace and its saved local DeepSeek
credential. The `deepseek-flash` run completed a read-only inspection and
identified the catalog request as `GET /api/models`, tracing it from the Vue
API client through `HomeView.vue` to the FastAPI router and its route contract
test. The run explicitly reported no edits, shell commands, or process starts.
The extension displayed per-call usage and timing metrics; the first two
visible calls used 24,690 input / 440 output tokens (2.79 s total, 1.02 s to
first token) and 25,439 input / 192 output tokens (1.95 s total, 0.94 s to
first token). This verifies workspace binding, saved-key model discovery and a
successful read-only VS Code request. It does not verify file writes, command
execution, or a full coding task. [Sanitized run evidence](evidence/testproj-vscode-inspection-20261012.json).

## Live webpage workspace creation and switching — 2026-10-12

In the external Edge xMind webpage, the workspace settings dialog created a
second local workspace from the existing `D:\CantorAI2026\xlang3` folder. The
workspace selector then exposed both saved entries; selecting
`D:\CantorAI2026\TestProj` switched the file explorer and active workspace back
to TestProj, which remains selected after closing the dialog. This verifies
webpage create/select flows on this local profile. Folder attachment and
agent execution in the newly selected workspace were not tested. The profile
now retains the xlang3 workspace as a second local entry.

The TestProj tree also contains a literal `%SystemDrive%\ProgramData` cache
directory tree with filesystem timestamps from 2026-10-12 22:33. Its origin is
not established, so it was left intact; no cleanup or ownership claim is made.

## Follow-up TestProj run and managed server launch guidance — 2026-10-12

A second live xMind run against `D:\CantorAI2026\TestProj` completed the Vue
production build and the real FastAPI catalog/statistics/detail checks. Its
xlang3-hosted pytest invocation stopped before collecting tests with
`AttributeError: 'PathAwareHookProxy' object has no attribute
'pytest_cmdline_main'`; this is a runner/runtime compatibility failure, not an
application assertion result. A subsequent foreground PowerShell command
started a hidden Vite dev server with `Start-Process` and timed out after 60
seconds. xMind marked the run failed and quarantined further effects because
the child-process outcome could not be established. The browser's separate
Edge session did pass live search, task-filter intersection, empty results,
reset and pagination through pages 1 and 2. No TestProj source edits are
attributed to this failed run.

The native `run_process` tool description now tells agents to avoid shell-level
`Start-Process`/`start` detachment and to request `background=true` so xMind
owns and joins long-running servers. Its provider-facing contract is covered
by `native_agent_process_http_contract`, which passes **1/1** after the Release
`xmind_server` and `xmind_cli` build. This guardrail does not turn the failed
run into a pass. At the time of this run, pytest lookup was pending a separate
xlang3 compatibility review. The branch candidate is now validated as
documented above; integration into xlang3 main and xMind's active runtime
remain open.

## Session-level interrupt — 2026-10-12

The native HTTP server now supports `POST /v1/sessions/{id}/interrupt` and
the console exposes `session-interrupt SESSION [false]`. A focused loopback
test confirms cancellation of a live streamed run, eventual persisted terminal
state, idle idempotence, 404 for a missing session, explicit rejection of
unsupported `resume=true`, and the CLI response shape. The route verifies the
session exists before handling interruption and responds when the cancellation
request is accepted; native cleanup and persisted settlement continue
asynchronously, matching the pinned OpenCode v2.0.26 contract. Durable inbox
admission and resume scheduling remain incomplete, so this does not establish
full session API parity. Release server and CLI builds succeeded; CTest passed
`native_agent_http_contract` **1/1**. The per-operation evidence and current
source hash are recorded in [the API inventory](OPENCODE_API_INVENTORY.json).

## Native CLI connection catalog management — 2026-10-12

Release `xmind_cli` and `xmind_server` build. The real server/CLI contract,
general HTTP CLI contract and connection-profile HTTP contract pass **3/3**.
CLI commands list, save, remove and select connection profiles using the
backend's revisioned catalog. Test coverage confirms Nexus metadata contains no
credentials, unauthenticated Nexus selection leaves Local active, retired IDs
cannot be reused, and insecure origins are rejected. This is metadata and
available-profile selection only; Nexus authentication/remote routing and
browser/VS Code selection remain open. [Exact Release CTest output](evidence/native-connection-profiles-cli-local-20261012.log).

## Authenticated connection-profile HTTP contract — 2026-10-12

Release `xmind.exe`, the connection-profile SQLite contract, the new
connection-profile HTTP contract and the existing backend owner/view-session
HTTP contract build. Focused CTest passes **3/3**. The new loopback HTTP test
verifies owner authentication, origin-bound view access, Local initialization,
catalog revision checks, rejection of unknown credential fields, an explicit
`not_connected` Nexus status, fail-closed Nexus selection and SQLite restart
recovery. This adds authenticated catalog read/configuration and Local
selection; it does not provide a browser/CLI picker, Nexus authentication or
remote command/event routing. [Exact Release CTest output](evidence/native-connection-profiles-http-local-20261012.log).

## xlang3 main integration and native Release gate — 2026-10-12

The xMind Release build uses the pushed xlang3 `main` revision
`f0c425cff5b6bf47f18144b31e4dc1ac16c9a9f7`, with CPython bridging disabled.
The complete native CTest suite passes **138/138** contracts in **173.58
seconds**. This includes the schema-17 populated migration and foreign-key
checks, direct-agent budget/run contracts, process identity drift handling,
the MCP/A2A and graph contracts, and local profile/view/provider-configuration
integrations. The migration preserves existing budget records and dependent
foreign keys while adding the `native.direct-agent` zero-child policy. The
process contract verifies a changed trusted executable fails the run before a
follow-up model request. Full output is preserved in
[CTest evidence](evidence/native-release-ctest-schema17-20261012.log).

This 2026-10-12 source Release gate did not validate xlang3-hosted pure-Python
`cryptography` or pytest. The later xlang3 candidate branch now exports mmap
buffers and passes the TestProj pytest suite, but remains outside this pinned
main build. This source Release gate also does not establish a complete
installed VS Code/IDE or live-provider acceptance run.

## MCP catalogue invalidation during a run — 2026-10-12

Validated `tools/list_changed`, `prompts/list_changed` and
`resources/list_changed` notifications and unplanned subscription closure now
set a run-owned invalidation flag shared with the registry. The registry checks before proposing and again after
approval but before remote dispatch. A change during approval durably fails
the claimed operation without calling the peer. AgentRunner returns a bounded
error to the model, refreshes the configured catalogue in the same run, and
lets the model reconsider with current definitions. A pinned graph MCP node
fails closed on catalogue drift, since its immutable alias/revision cannot be
silently rebound. Synthetic HTTP AgentRunner and graph contracts cover these
paths, including absence of a stale peer effect. Focused Release tests passed
**11/11** (AgentRunner, graph MCP, registry effects, transports and SDK
peers). [Exact Release CTest output](evidence/native-mcp-catalogue-invalidation-local-20261012.log).
Crash recovery/reconnect and live external-peer acceptance remain open.

## MCP 2026-07-28 subscriptions/listen for agents and graphs — 2026-10-12

The shared `McpToolClient::listen_subscriptions` validator covers stdio and
HTTP. AgentRunner now opens a separate listener for modern peers that advertise
list-change notifications, records acknowledgement/notification/closure events
to the run journal as soon as the acknowledgement is validated, and joins the
listener before terminal state is committed.
Events include only validated method and refresh metadata: payloads and resource
URIs are excluded, and each listener is capped at 64 notification events with
one overflow record. Held-open synthetic HTTP AgentRunner integrations send 70
notifications with credential-shaped values and verify sanitization, the cap,
overflow marker, cancellation and durable closure
before run completion. Direct graph MCP tool children use the same observer;
stdio graph tests verify closure for approved, denied and cancelled runs, and
an HTTP graph test verifies that unexpected listener completion invalidates a
pinned registry and closes before child failure. The HTTP AgentRunner integration also verifies an MCP effect waits
for controller approval and provider token metrics persist. Dedicated stdio
and HTTP transport contracts verify acknowledgement ordering. Graph/MCP,
AgentRunner, stdio/HTTP transport and official modern/legacy SDK contracts pass
**10/10** at the subscription-lifecycle checkpoint.
Crash recovery/reconnect and live external-peer acceptance remain open. [Exact Release CTest output](evidence/native-mcp-subscription-lifecycle-local-20261012.log);
[transport scope](native-mcp-http.md).

## MCP 2026-07-28 HTTP subscriptions/listen — 2026-10-12

The Release `agentflow_mcp_http_readonly_contract` target builds, and the
focused CTest passes **1/1**. Its real loopback Streamable HTTP peer exercises a
request-scoped SSE stream, header binding, first-message acknowledgement,
acknowledged-filter validation, resource-update and tools-list notifications,
subscription-ID correlation and graceful completion. A second connection
proves an unrequested prompts-list notification retires the owner. The public
native HTTP client exposes the accepted filter and validated notifications.
The separate AgentRunner HTTP integration proves held-open listener
cancellation, durable pre-terminal closure and an approval-gated tool effect;
see [the expanded lifecycle contract](evidence/native-mcp-subscription-lifecycle-local-20261012.log).
Reconnect, installed-client or live external-server acceptance remain
unverified. No external service or credential was used. The earlier transport
checkpoint's dependent MCP wire/client, graph-MCP, AgentRunner HTTP-MCP and
unified-server contracts passed **6/6**;
[exact CTest output](evidence/native-mcp-subscriptions-http-local-20261012.log).
See [transport scope](native-mcp-http.md).

## Live TestProj Model Zoo browser acceptance — 2026-10-12

The completed DeepSeek `deepseek-flash` xMind browser run used the real
`D:\CantorAI2026\TestProj` workspace and made one scoped `HomeView.vue`
change: a request token prevents an older catalog response from overwriting a
newer search/filter result. Its post-edit `npm.cmd run build` passed (Vite
6.4.4, 36 modules, exit code 0). In the rendered Model Zoo page, search,
Text Generation filtering, no-result state, reset, page 2, name sorting and
GPT-2 detail navigation all worked against the already-running frontend/API.
The page was restored to its default catalog view. This verifies an actual
browser flow in the TestProj workspace, not xMind's own browser UI or the
standalone project runtime lifecycle. xMind did not start the frontend/API;
pytest, small-viewport breakpoints and project Git provenance remain
unverified. Exact run ID, source-file hash, checks and limits are recorded in
[the live-browser evidence](evidence/testproj-model-zoo-live-browser-20261012.json).

## Current Release native gate — 2026-10-11

The complete Release `ALL_BUILD` succeeded after the benchmark guard deferred
two attempts while xlang3 benchmarks were active. The current Release CTest run
passes **136/136** contracts in 646.64 seconds. It includes the real
rust-analyzer definition, references, hover, formatting and diagnostics flow.
An earlier run returned `null` within the old 15-second retry. The contract now
allows up to 60 seconds for semantic readiness; focused and full reruns passed.
The timing evidence does not establish whether cold indexing or system load
caused the original miss. The focused language-server rerun passed **2/2**,
including its Cargo fixture prerequisite. The long local-profile lifecycle,
local-view and provider-config contracts also pass.
See [current full Release CTest evidence](evidence/native-current-release-ctest-retry-20261011.log),
[focused rust-analyzer evidence](evidence/native-rust-analyzer-focused-20261011.log),
and the [earlier run that exposed the cold-indexing timing issue](evidence/native-current-release-ctest-20261011.log).
The [focused profile retry record](evidence/native-lsp-retry-focused-20261011.md),
[earlier complete Release run](evidence/native-full-release-ctest-20261011.log),
and [intervening 133/134 run](evidence/native-lsp-retry-ctest-20261011.log)
remain historical results. Local Release validation does not claim installed
IDE acceptance.

## Current browser and VS Code source suites — 2026-10-11

At the same checkout, `npm test` in `extensions/vscode` passes **310/310** and
`node --test views/browser/tests/*.test.cjs` from the repository root passes
**95/95**. Exact current TAP output is preserved in
[VS Code evidence](evidence/vscode-current-source-20261011.log) and
[browser evidence](evidence/browser-current-source-20261011.log). The installed
VS Code package is `agentflow-local.agentflow@0.1.10`. The local browser page
returns the xMind UI successfully, but its authenticated backend health route
returns 401 without the browser's HttpOnly view session. External Edge visual
inspection could not proceed because the computer-use helper could not
confidently verify the current URL. No rendered VS Code or authenticated live
browser action is claimed here.

## Live model-driven coding run reached its turn limit — 2026-10-11

A local xMind browser-profile run against a Model Zoo test workspace used the
saved DeepSeek profile, inspected the Vue project and completed a real
`npm.cmd run build` with exit code 0. The model then proposed several scoped
`HomeView.vue` edits; the run ultimately ended with `model_turn_limit` before
finishing its redesign. One observed model call reported 47,875 input tokens.
The page changes were completed manually and separately verified, so they do
not count as a completed xMind coding-agent run. This is negative live
acceptance evidence for a multi-step coding task; the local service binary's
exact source revision was not established. Reduce repeated context cost and
verify the effective model-turn budget before claiming smooth coding-agent
completion.

## Live saved-key native CLI process run — 2026-10-11

The current `build/native/contract-bin/Release` server and CLI completed one
actual DeepSeek `deepseek-flash` agent turn. A disposable backend imported the
saved provider keys, discovered models through the backend using the saved
DeepSeek key, and selected `deepseek-flash`; the key never entered the CLI
request or output. The model proposed a single PowerShell `Write-Output`
command. The CLI review was checked against the exact executable, fixed
arguments and workspace before approval. Native execution returned exit code
zero and the expected stdout marker, then persisted a succeeded process
operation, completed run, conversation history and actual token/timing metrics.
The isolated database and workspace were not added to the repository. This
proves one live saved-key provider and foreground process path through the
native CLI, not browser/VS Code process acceptance, shell discovery, or broad
coding parity. [Sanitized evidence](evidence/native-live-cli-process-acceptance-20261011.json).

The saved YAML profiles are intentionally key-only when their `model` field is
blank. In that state the backend can enumerate profiles and use their saved
keys for model discovery, but it cannot execute an agent until a model is
selected. The provider/model picker must perform that selection; requesting the
secret in the browser would violate the credential boundary.

## Shared-client saved-provider status — 2026-10-11

VSIX `0.1.9` is installed as `agentflow-local.agentflow@0.1.9`. Settings now
states explicitly that a selected saved provider key is available to the local
backend and will be reused; the replacement field stays blank. The actual
browser Settings panel displayed this text for DeepSeek. A live click on
**Fetch models** then returned two DeepSeek model IDs from the selected saved
profile, without a key in the request from the page. The selected profile's
existing connectivity run also returned `OK`. This verifies saved-key reuse
for this DeepSeek profile only. One browser file-creation run is described
below; other providers and broader coding workflows were not retested. The
packaged native-runtime verifier passed
with 1,899 files; VS Code tests pass **298/298** and browser tests **85/85**.
VS Code installation is verified, while activation/rendering in an existing
editor window remains unverified. This change does not validate the broader
provider matrix, native process coding, or full OpenCode parity. See [sanitized
browser evidence](evidence/provider-key-status-live-browser-20261011.json).

## Live browser coding-file approval — 2026-10-11

The connected browser used the saved DeepSeek profile to complete a real
`create_file` tool call. The UI displayed the exact path and one-line content,
required approval, and recorded the operation as succeeded. The file was
created under the Git-ignored `build/` directory, its exact bytes were checked,
and the disposable file was removed; the working tree remained clean. Run
`90049479d1dc07d02ea5740767c7f9f7` reported provider input/output metrics in
the sidebar. The running native executable identifies revision `3681c2e`, an
ancestor of this source, but differs from the current Release binary. This is
one actual browser create-file path, not VS Code acceptance, process/shell
execution, or broad coding parity. See [sanitized run evidence](evidence/live-deepseek-browser-create-file-20261011.json).

## Managed trusted process-profile configuration — 2026-10-11

Commit `b0dd491` wires `.config/processes.yaml` into the managed browser and
VS Code development launchers and supports a machine-level VS Code setting for
packaged installs. The Release `xmind` target builds. The focused native
local-profile integration contract passes: trusted profile import, refusal of
an implicit config change, protected owner handoff, and preservation of SQLite
sessions, history and graph results across restart. The VS Code suite passes
**298/298** and the browser suite **85/85**.

VS Code offers a confirmation-gated **xMind: Apply Trusted Runtime
Configuration** command. The host requests `--upgrade-backend` only after the
user confirms; a successful launch consumes the one-shot request. The command
and argument handoff have isolated host tests. VS Code package `0.1.8` was
built as a 1,938-file, 18.02 MB VSIX with the verified native runtime and
installed locally; `code --list-extensions` reports
`agentflow-local.agentflow@0.1.8`. The real backend upgrade prompt and actual
managed-client process execution remain unverified.

The focused native local-view contract now passes against the Release binary
with a configured profile; the authenticated view reads only profile ID,
revision and timeout. The full native CTest gate remains unrerun, and rendered
managed-client process execution is unverified. A fresh Windows profile now
receives a built-in PowerShell process profile (with system `cmd.exe` fallback)
only when the profile has no saved process registry; built-in shell commands
run without per-command approval prompts. Existing custom or explicitly empty registries
are preserved. The Release process-configuration, process-executor and
agent-process HTTP contracts pass **3/3**, and the fresh managed local-view
integration contract passes **1/1**. Exact implementation and limits are in
[native process tools](native-process-tools.md).

## Native shell-profile approval check — 2026-10-11

The Release `agentflow_process_executor_contract` now invokes the installed
PowerShell executable through a trusted profile. It verifies that the exact
executable, fixed `-NoLogo -NoProfile -NonInteractive -Command` prefix and
script are recorded in a pending approval; the disposable file effect is
absent until approval, then the exact file and captured/journalled output are
verified. `native_process_executor_contract` passes (**1/1**). The Release
`xmind`, `xmind_server`, `xmind_cli` and `xmind_admin` targets build, and
`native_agent_process_http_contract` passes (**1/1**) for the existing actual
server/CLI process lifecycle contract.

This verifies a configured PowerShell profile at the native executor and a
separate process path through HTTP/CLI. The tests do not combine PowerShell
with a model-driven server run, nor render a shell approval in browser or VS
Code. Shell discovery and command semantics parity remain incomplete; full
native CTest remains unrun.

## Interactive CLI cancellation and detach — 2026-10-11

The console now reads input while it observes an active single-agent or graph
run. `/cancel` requests backend cancellation and keeps observing the terminal
event; `/exit` detaches observation while the server-owned run continues.
Other typed lines remain queued for the next prompt. The behavior is covered
by focused Release CTest contracts for streamed agent runs, graph runs and
approval/graph checkpoints. The seven relevant native/CLI contracts passed
locally (**7/7**), including context, skill catalogue, HTTP CLI and provider
profile commands. This is not a full native gate or hosted/package acceptance;
the interactive contract is documented in
[native interactive CLI](native-interactive-cli.md).

## MCP resources and prompts — 2026-10-11

The Release build succeeds for the dynamic-plan MCP contract, MCP client and
effect contracts, AgentRunner and graph execution. Focused CTest passes **10/10**:
`native_dynamic_plan_mcp_contract`, `native_mcp_client_contract`,
`native_mcp_effect_contract`, `native_mcp_http_readonly_contract`, graph
stdio/HTTP MCP, AgentRunner HTTP MCP, modern and legacy official MCP SDK peers,
and unified HTTP MCP. The stdio peers exercise resource and prompt
listing/retrieval; a local Streamable HTTP peer checks pagination and correlated
calls. The pinned official MCP server SDK 2.3.1 now exercises resource/prompt
discovery and retrieval through the configured native HTTP client and approval
registry, including allow, deny and SQLite reopen. Tool effects retain actual
results, and configured-credential reflection is rejected safely. The combined
renderer and browser-controller suite passes **117/117**.

This is not a full native CTest run on the new commit. Broader HTTP/SSE
interoperability, installed VS Code coding, and live MCP coding remain
unverified.

## Native editor formatting checkpoint — 2026-10-11

The VS Code extension now registers a document-formatting provider for each
configured language-server profile. Formatting sends the current unsaved
editor buffer, workspace authority and VS Code indentation settings through the
native authenticated HTTP bridge. The native client checks that the configured
server advertises formatting support; results are validated for document
bounds and overlapping edits before VS Code applies them. The model-visible
AgentRunner language-server tool remains read-only and accepts an optional
folder ID from the run-pinned workspace snapshot.

The Release `agentflow_language_server_contract` target builds, and its direct
run against the installed Rust Analyzer passes real stdio initialization,
formatting, semantic navigation, diagnostics, AgentRunner read-only checks,
authenticated formatting over native HTTP and xlang3 SQLite initialization.
The focused VS Code/client test command passes **45/45**. The native contract
alone did not verify rendered IDE behavior; the Development Host result below
subsequently proves formatting and save for the configured Rust profile.
Packaged IDE acceptance, automatic formatter discovery/configuration and a
full edit/build/diagnose loop remain unverified.
Exact scope is recorded in
[formatting evidence](evidence/native-language-server-formatting-local-20261011.json).
Checkpoint `4209649` is pushed to `checkpoint/native-persistence-m1`.

The packaged extension, formatter discovery and model-driven repair loop remain
distinct from this source-host check; do not infer their acceptance from the
real native Rust Analyzer contract.

After the native contract exposed an empty language-server catalogue in the
provider-profile runtime, that runtime now forwards profile metadata and user
queries to its shared `ExecutionPlatform`. A rebuilt native backend and
rendered VS Code Extension Development Host then formatted and saved
`D:\CantorAI2026\LspFormatFixture\main.rs` using the configured Rust Analyzer;
`rustfmt --check` passed on the saved file. The opened workspace was a disposable
fixture, provider inference was zero, and the extension suite passed **293/293**
and browser suite **85/85**. This demonstrates the in-editor formatting path
from trusted YAML profile through native authority-bound HTTP into the editor;
the packaged extension and a full model-driven edit/build/diagnose repair loop
remain unverified. Updated scope is in
[formatting evidence](evidence/native-language-server-formatting-local-20261011.json).

## VS Code language-server navigation checkpoint — 2026-10-11

The AgentRunner contract now performs provider-driven `language_server` tool
calls against admitted folders with real Rust Analyzer. During one run it adds
a third folder and verifies the pinned run rejects it; the next run uses
revision 3 and returns the new folder's distinct symbol. The synthetic-provider
native contract passes (**1/1**); it does not claim live model or installed
multi-folder VS Code acceptance.

Configured language servers now back VS Code Go to Definition, Find References
and Hover alongside the existing diagnostics collection. Requests are bound to
the active workspace and unsaved editor buffer; returned locations are confined
to that workspace. Stale, cancelled and disconnected responses cannot surface.
The full VS Code extension suite passes **290/290**. The focused native
Rust Analyzer Release contract passes against the pinned xlang3 compatibility
worktree and verifies native HTTP symbols, unsaved document symbols,
diagnostics, workspace authority, real definitions/references/hover, and a
definition query through the authenticated browser-view API. The LSP bridge
retries bounded `ContentModified` replies while Rust Analyzer updates its
analysis. Actual installed-IDE navigation and formatter behavior remain
unverified. See [language-server scope](native-language-server.md) and the
[parity baseline](PARITY.md).

The graph service and unified graph HTTP/CLI tests pass **2/2** after aligning
their expected imported-agent metadata with the nullable `tools` field. The
xlang3 repository contract passes **1/1** with the pinned compatibility
worktree's SQLite package; the separate `build/native` cache points at an older
package and is not the configured project build.

## Saved provider key reuse from the local webpage backend — 2026-10-11

The backend attached to the open same-machine webpage reported the active
DeepSeek profile and returned `deepseek-flash` and `deepseek-v4-pro` for a
provider-catalogue request containing only profile ID, route and revision. No
provider key was sent to or read back by the page. This confirms the native
runtime used its saved credential; it does not claim a rendered-page click or
live inference. The 0.1.6 VS Code extension now confirms in Settings when model
discovery used the saved key. Its 290-test extension suite, 85-test browser
suite and verified VSIX packaging passed. The package was installed and the
regular TestProj VS Code host restarted; a fresh rendered-sidebar check remains
unverified because desktop window capture returned an access-denied error. See
[live saved-key evidence](evidence/provider-saved-key-live-backend-20261011.json).

## Full current local gate — 2026-10-10

The current Release `ALL_BUILD` passes the complete native suite **122/122**
in **547.58 seconds**, including all previously failing managed-profile/view,
xlang3 repository and owner-handoff contracts. The source also passes **272/272
VS Code extension** and **75/75 browser** tests, followed by actual native/browser
integration for a paused/resumed file-read graph, cookie/history retention over
view/backend restart and no replay. Exact runtime/source hashes and raw logs are
in [gate evidence](evidence/native-122-and-views-local-20261010.json). This is
local validation only; installed VS Code rendering and live coding in the editor
remain unverified.

## Rebuilt local browser provider acceptance — 2026-10-10

The rebuilt local development runtime at source `65f20505321e67dffcb495ff38e735781a9fdc8e`
loaded the repository `.config/providers.yaml` into its native backend. In the
same-machine browser, live model discovery returned 13 Claude, 11 Gemini, 2
DeepSeek and 20 OpenAI models. A short real text inference returned `OK` for
Claude Haiku 4.5, Gemini 3.8 Flash, DeepSeek Flash and OpenAI gpt-5.6-sol; the
page rendered each run's provider usage and measured latency without asking for
a provider key or server token. The exact four run IDs, metrics, xlang3/runtime
identity and limits are recorded in
[live provider evidence](evidence/live-provider-profiles-current-native-browser-20261010.json).
This is local development-runtime browser acceptance only. It does not prove
streaming, tool/edit execution or installed VS Code behavior.

## Same-machine webpage attach and current hosted views — 2026-10-10

The active local browser launcher advertises `http://127.0.0.1:60430/ui/`;
the route returns HTTP 200 and serves the workspace selector and right-sidebar
markup without a server-token field. Local browser connection and adapter
contracts pass **20/20**, covering automatic same-machine session enrollment,
refresh, workspace selection and folder picking. The already-open `60415` tab
shows an authenticated local connection and history, but retains an older
Connect control; it is not fresh rendered acceptance of the active `60430`
view.

Hosted run
[38099857512](https://github.com/xlang-foundation/xMind/actions/runs/38099857512)
passes **264 VS Code and 67 browser contracts** and confirms tracked source,
vendored files and generated browser assets were unchanged during the suite.
It does not execute the native backend or prove installed-editor interaction.
Native run
[38098811196](https://github.com/xlang-foundation/xMind/actions/runs/38098811196)
is still compiling the pinned xlang3 runtime and full native suite; the newer
run for `bd7aa30` is queued behind it. The guarded local native build correctly
deferred before configuration because a live xlang3 performance benchmark is
running. Native release acceptance remains open.

## Browser recheck — 2026-10-10

The open `60415` page connects to the same-machine xMind backend without a
token prompt. Its selector shows the local xMind and TestProj workspaces plus a
provider-test workspace; its right sidebar restores conversation history, run
metrics and saved Claude, Gemini, DeepSeek and OpenAI profiles/models. This
adapter is attached to the fixed-workspace `xmind serve` backend, so **Add
workspace** is disabled with an explanatory hint. Workspace file browsing still
reports that this connected native runtime needs an update.

The profile-aware page at `60422` has **Add workspace** enabled and selects
TestProj, but this launched profile has no provider profiles and its current
native runtime also reports that workspace file browsing needs an update. The
new shared-provider import contract is part of hosted run
[38093398490](https://github.com/xlang-foundation/xMind/actions/runs/38093398490),
which is still building the pinned xlang3 runtime and native contract set. The
earlier failure on run `38091048320` was the managed-profile contract hitting a
transient Windows lock on `native-runtime-manifest.json`; it predates commit
`c5f8fab`, which added bounded retry, so the newer run is the acceptance gate
for that repair. The standalone webpage contracts pass **65/65**, including
tokenless local Connect and workspace-mode behavior. Chrome/Edge were not
available through the current browser-control session; the live observations
above came from the open Codex browser tabs.

## Schema-v14 checkpoint — 2026-10-10

Commit `15c87c0` passes all **118/118** native Release CTest contracts in
**366.56 seconds** after correcting legacy test fixtures and schema-version
assertions for persistent named-agent selection. The current-schema legacy owner
handoff contract also passes without dropping saved data. [Exact local log and
provenance](evidence/native-schema14-local-20261010.json). Webpage and extension
sources did not change; the already documented rendered webpage and VS Code
workspace evidence records their latest live check.

## Local workspace file browsing — 2026-10-10

The current Edge page at `60431` auto-connected without a token prompt and
showed the local `xMind` workspace selector and **Add workspace** control. Its
file tree reported that the connected packaged native runtime needs an update;
the actual workspace listing was not returned. The running binary was
`.agentflow/dev-browser-runtime-current/xmind.exe`
(`fbcbf957195397da26cfe7143c976d727d2836337d67c67731429179c051c1dc`). Exact
manual observation: [workspace-read browser evidence](evidence/browser-local-workspace-read-live-20261010.json).

The browser now explains this runtime mismatch instead of displaying the raw
401. `Native/tests/agent_http_contract.mjs` also checks that a real native view
session can list workspace files and preview UTF-8 content. That C++/xlang3
contract and a current packaged runtime are pending the hosted gate; the live
observation does not establish workspace browsing success.

The same-machine page requires no user-entered server access token. The
browser adapter resolves its matching launcher credential when the page
connects, including if the view started just before the active launch record
appeared. The focused connection and adapter suites pass **26/26**. Rendered
inspection found that the open `60415` page is tokenless but its
**Add workspace** control is disabled, so it does not prove the profile-aware
folder registration flow. Port `60405` still serves an obsolete token-entry
page. The profile-aware page and current-runtime workspace read remain the
required live acceptance.

Commit `31987db` makes the fixed-workspace limitation visible in the left
workspace pane. Its isolated hosted workflow passes **264 VS Code and 65
browser contracts**; source, vendor and generated-asset integrity checks pass.
This does not establish native execution or rendered local-profile acceptance.
[Hosted run](https://github.com/xlang-foundation/xMind/actions/runs/38092318086).

## Rendered webpage and VS Code workspace — 2026-10-10

The current VS Code Extension Development Host rendered xMind in its right
sidebar and reported the selected opened folder as its native backend root. A
fresh current-source webpage was launched from **xMind: Open Browser View**,
connected to that same temporary preview backend, and displayed the identical
folder beside its workspace selector. Refresh restored the authenticated
connection and retained the correct root. This live path exposed the mismatch
between the browser's multi-profile catalogue and VS Code's single-folder
`serve` backend; the access adapter now presents the authenticated `/v1/workspace`
as a fixed one-item catalogue in that mode. The page disables Add workspace and
explains that profile-aware local `xmind view` is needed to add or switch
profiles. The browser DOM and adapter test exercises both modes.

Browser tests passed **60/60** after the adapter fallback, then the focused
workspace/adapter suite passed **13/13** after the rendered hint change. The
current assets were rebuilt and the live page was refreshed to show that hint.
The native executable was then rebuilt from current source and staged in a
verified development runtime. Against a fresh, isolated profile root, its full
local view advertised `can_add: true`; the live same-origin browser adapter
registered `D:\CantorAI2026\TestProj`, selected it, retained both profiles, and
rotated the HttpOnly session cookie. In the rendered standalone page, **Add
workspace** opened the folder-path dialog, registered and selected `TestProj`,
and remained selected after refresh. Switching back to `xMind` changed the
displayed server workspace accordingly. The VS Code-attached page remains
fixed to its opened folder. Browser webpage/adapter contracts pass **16/16**.
This check made no model request or workspace-file effect. Model-backed
rendered answer/file approval and installed-client acceptance remain separate
work. Evidence and scope are in [browser-view.md](browser-view.md).
The tested launcher and adapter change are commits `36d995a` and `661351e` on
`checkpoint/native-persistence-m1`.

At source `4701255`, the native local-view contract passed in 188.95 seconds,
including actual browser-cookie workspace registration and production browser
controller/SSE use against a compiled native owner. It also drove the production
VS Code backend/controller through a native graph pause, resumed incremental
events, read and approved file creation, and matched disk bytes to the effect
receipt. The native results, restart/replay, feed limits and revocation are
captured in [exact local evidence](evidence/native-local-view-browser-sse-local.json).
Provider requests were zero; VS Code APIs were fixtures, and this does not claim
rendered browser/IDE or installed-client acceptance.

The shared patch-review client candidate passed **222 extension / 41 browser**
contracts with all 44 input and 12 generated asset hashes unchanged. It renders
all four per-file patch actions and uses revalidated read-only comparisons;
malformed proposals cannot receive an Allow through either adapter. These are
source/DOM checks, not installed or native patch acceptance.
[Scope and evidence](patch-review.md).

The earlier native patch primitive source `a7f484a` compiled on Windows but its
complete gate failed: **97/99 contracts passed**, with failures in workspace
move effects and the parallel delegation deadline. New diagnostics preserve
the move phase/Win32 code and actual parent/child state on timeout. Current
103-contract candidate compilation/execution remains pending; no patch release is accepted.
[Exact failed job](https://github.com/xlang-foundation/xMind/actions/runs/37974972815).

Native general-patch parsing/preparation and actual-workspace planning are source increments, with a new
contracts included in the complete 103-contract native manifest. Compilation and
execution are pending the isolated workflow. The new model adapter is connected
in source to approval-enabled agents/coding children and declared graph steps;
it has not been installed or accepted by a native gate. Expanded filesystem tests cover source/
parent identities, destination absence, private/link paths, Unicode collisions,
overlapping targets and aggregate review bounds. Approval/effect integration remains
required. [Implementation and remaining delivery](native-file-patch.md).

Backend-only deletion and move effects are implemented in source, with exclusive
handle/snapshot checks, no-overwrite rename, post-effect verification and explicit
uncertainty handling. Disposable-filesystem contract source covers actual delete,
rename/content update, missing folders and rejected stale/private/link operations.
Compilation and execution remain pending. No installed patch acceptance is claimed.

The new per-file patch executor now binds immutable batch/file proposals to the
native permission waiter, claim and outcome journal. Contract source covers actual
approved effects and xlang3/SQLite receipts, caller-mutation resistance, denial,
changed guidance, stale sources, deliberate journal failure and restart quarantine
without replay. Execution is pending. Native multi-file coordination is now in
source, with all-proposal preflight, full requested manifests, observed partial
outcomes and verified reuse of this batch's created parent directories. New
contract source covers mixed effects, shared folders, denial/cancellation, stale
later files, replaced directories and restart without replay. These tests have
not run for the candidate. Model input/guidance and graph integration are now
source candidates, with new native/HTTP/CLI contract source and expanded graph
checks. Move approvals bind source/destination guidance; later files refresh
scope delivery before another proposal. Own `AGENTS.md` changes preserve a
partial outcome and require a fresh model turn. Native execution and installed
client delivery remain incomplete.

The shared sidebar approval-review candidate passed the complete isolated
**217 extension / 39 browser** gate. A persistent review notice and bounded
activity log address the installed test's offscreen proposal; pending review
suppresses automatic scroll movement. Run/workspace reset clears old controls.
These are source/DOM checks. The renderer is installed in the recovery 0.1.3
package, but live pending-approval layout/navigation acceptance remains pending.
[Behavior and validation scope](sidebar-approval-review.md).

The native regex-search implementation at exact source `5c8c918` passed its
[isolated Windows workflow](https://github.com/xlang-foundation/xMind/actions/runs/37968627266):
all 98 native contracts in 274.50 seconds, with exact expected/registered/passed
identities and receipt hashes verified. Actual filesystem/HTTP regressions cover
large files, UTF-8, scoped paths, regex failures, read/output budgets and link
boundaries. All 39 browser controller contracts and packaged asset checks passed.
Independent verification matched both archive digests, all 32 development bundle
files and all 1,863 packaged runtime files, including RE2/Abseil licenses.
Provider replies in native agent contracts are synthetic; actual provider-driven
regex and installed client regex acceptance remain pending. Local native workload
is deferred while the separate xlang3 benchmark is active.
[Behavior and limits](native-content-search.md), [exact evidence](evidence/native-regex-hosted.json).

The VS Code preview's saved profile migration was observed complete, with an
approval-enabled backend rooted at `D:\CantorAI2026\TestProj`. A subsequent
old-conversation run failed with `context_unavailable` before provider execution;
old and current authority bindings differed. In the fresh conversation, an actual
OpenAI request proposed `hello.py`; approval through the installed sidebar led to
`create_file · succeeded` and a completed run. The file's exact bytes were checked
independently on disk. Both responses displayed provider token metrics. This is
installed 0.1.1 / native `5d8b175` writing acceptance, not acceptance of the newer
regex-search candidate. [Exact evidence](evidence/vscode-file-creation-live.json).
The recovery-message source passed 215/39 client contracts and is now installed
in client 0.1.3. Reload restored the completed run against the same native owner;
a fresh conversation was observed ready at the correct root. No new inference
or repair of the failed context receipts is claimed by that update.
[Recovery and exact scope](saved-context-recovery.md).

The local ignore-rule candidate passed **98 native contracts in 209.78 seconds**,
with all 637 mapped inputs unchanged and exact expected/registered/passed
manifests matched. **215 extension / 39 browser** contracts passed separately.
Actual OpenAI content search returned only two allowed fixture paths, excluded
generated/private data and retained exact history across an xlang3/SQLite restart,
with zero mutation operations. Native synthetic-peer checks also persist filtered
discovery/search results. Actual Git decisions plus explicit fixture-defined
search overrides match the native corpus; the first ripgrep comparison's two
POSIX-class/brace differences remain recorded. The candidate was not installed
over the pending VS Code migration. Global/parent/external-worktree ignore
configuration, full dialect/search parity and installed writing remain incomplete.
[Behavior and limits](native-ignore.md), [exact evidence](evidence/native-ignore-local.json).

The native glob-discovery candidate passed **98 native contracts in 208.70
seconds**, with all 635 mapped source inputs unchanged and exact manifests
matched. The unchanged view sources passed **215 extension / 39 browser**
contracts. An actual OpenAI run discovered four exact paths, including Unicode
and public hidden names, excluded a synthetic private configuration file,
created no mutation operations and retained its history after an xlang3/SQLite
restart. Synthetic native contracts separately exercise graph and delegated
discovery. An actual depth-boundary fixture passes with a shared enumeration
buffer; first-gate catalogue assertion failures remain retained. The candidate
was not installed over the pending VS Code migration. Gitignore/global-ignore
rules, escaped metacharacters and full search/coding parity remain incomplete.
[Behavior and scope](native-glob.md), [exact evidence](evidence/native-glob-local.json).

The bounded native file-reader candidate passed **98 native contracts in
207.52 seconds**, with all 633 mapped source inputs unchanged and exact
expected/registered/passed manifests matched. **215 extension / 39 browser**
contracts passed with unchanged source and asset bytes. Actual OpenAI
`gpt-6.1-sol` inference read the last two lines of a 4.29 MB file through the
native tool loop, quoted the exact Unicode text and retained its complete
history across an embedded-xlang3 SQLite restart. The reader made no mutation
operations. The initial live harness incorrectly asserted execution readiness
before provider/model activation; that failure remains recorded.
This candidate was not installed over the pending VS Code migration.
Directory/media paging, omitted long-line suffix recovery and full coding-tool
parity remain incomplete. [Behavior and scope](native-file-pages.md),
[exact evidence](evidence/native-file-pages-local.json).

Post-stop publication rollback and operator recovery passed **98 native
contracts in 205.33 seconds**, with 631 inputs unchanged, plus **198 extension /
39 browser tests**. The real source process exits before a deferred foreign-key
publication failure; the CLI reports failure, native rollback leaves no ticket
or fault row and saved sessions remain intact. An explicit fixture operator
repair and separate preparation then permit exact bootstrap/activation. No
successful stop response is fabricated. Rollback-call failure itself is not
injected. The installed 0.1.1 confirmation remains pending and this source was
not deployed over it. [Failure path and limits](native-legacy-publication-fault.md),
[exact evidence](evidence/native-legacy-fault-local.json).

Version **0.1.1** passed a fresh 98/198/39 local gate, independent 1,933-entry
VSIX verification and actual complete-runtime server/session/retirement smoke.
It is installed in the existing normal TestProj window; old transcript, selected
OpenAI model and supplied usage remain visible without another key prompt.
VS Code's obsolete-directory cleanup required restoring the old runtime name
and missing files from its verified pre-update copy; original native PID/birth
and database remained unchanged at that historical checkpoint. The former
migration path is superseded by the current-format fresh installation; no
migration confirmation is required. Rendered writing remains unverified.
[Package, repair and installed scope](native-legacy-package.md),
[exact evidence](evidence/native-legacy-package-local.json).

Native legacy stop and managed adapter adoption passed **98 contracts in 196.55
seconds**, with all 631 mapped inputs unchanged. Actual production fixtures
authenticate and revalidate a legacy source, stop it under an embedded-xlang3
SQLite writer fence, publish/recover its exact ticket and activate the same
database. The thin adapter dispatches stop once, recovers after deliberately
lost stdout and reconnects on host reload. All **198 extension and 39 browser
tests** passed with unchanged view/asset bytes. Package provenance, confirmation
choice and VS Code storage are fixtures; no providers, installed migration or
file effects were tested. The installed TestProj source remains read-only.
Verified packaging/installation, live continuity and rendered create/edit
approvals remain pending. [Native stop/adoption and limits](native-legacy-stop.md),
[exact evidence and retained failure](evidence/native-legacy-stop-local.json).

Authenticated native legacy-owner preflight passed **98 contracts in 188.46
seconds**, with 631 mapped inputs unchanged. Actual listener/process handles,
source image/hash, database/workspace command line and authenticated workspace
identity are checked before any migration. Wrong credentials and bindings fail;
the real fixture process stays running with sessions unchanged. This increment
does not stop an owner, publish a migration ticket or upgrade the installed
preview. The 39 view inputs are unchanged from the previous 197/39 passing
checkpoint. Product shutdown, extension legacy adoption and rendered writing
remain pending. [Preflight guide](native-legacy-preflight.md),
[exact evidence](evidence/native-legacy-preflight-local.json).

Native post-exit legacy preparation, qualified closed bootstrap and exact
activation passed **98 native contracts in 193.73 seconds**, with all 629
mapped inputs unchanged. The actual CLI/server fixture rejected a live source,
preserved a saved session and activated the same database after its owned idle
fixture was killed by the test. Native history/credential/rollback checks also
passed. This does not implement product legacy shutdown or installed migration.
No provider or file effects occurred. The 39 view inputs are unchanged from the
previous 197/39 passing checkpoint; those suites were not rerun. The earlier
hosted 8e07ef0 handoff failure is retained, and native persistence now imports
xlang3's existing `_sqlite3` binding explicitly. New hosted success remains
unclaimed. [Preparation and limits](native-legacy-owner.md),
[exact evidence](evidence/native-legacy-owner-local.json).

The managed-owner adapter and native Windows process-exit observer passed
**97 native contracts in 196.96 seconds**, with 626 mapped inputs unchanged.
The actual production thin launcher completed same-database retirement,
qualified startup and activation using the real OS observer, including a
deliberately lost response without resending retirement and a host reload.
VS Code state/environment and deployment metadata remain fixtures; no provider
or file effects were tested. All **197 extension and 39 browser checks** passed
with 39 view inputs unchanged. The isolated SDK follow-up is pinned to pushed
`7b8b32ae`; its full 11-case performance gate passed and its two baseline
correctness failures remain retained. Installed legacy migration, accepted
packaging and rendered file writing remain pending.
[Managed upgrade commands and limits](native-owner-upgrade-client.md),
[exact evidence](evidence/native-owner-adapter-local.json).

Target-bound retirement and qualified replacement startup passed **96 native
contracts in 198.23 seconds**, with all 623 mapped inputs unchanged and every
registered contract included. The actual production server retired and exited;
its replacement opened the same database with admission closed, retained saved
sessions and activated using its new receipt and workspace authority. Actual
Unicode workspace/package paths passed. Native schema migration and prepared
owner publication roll back together on commit failure. Fixture package
metadata and unused members remain synthetic; no provider calls or file
effects occurred. The production server supports the owner protocol when
started with an independently accepted runtime-manifest digest. Thin-client
adoption, explicit legacy migration and installed VS Code writing are still
pending. The installed `TestProj` backend remains read-only.
[Replacement protocol and limits](native-owner-replacement.md),
[exact evidence and retained attempts](evidence/native-owner-replacement-local.json).

The required SDK fix is committed and pushed on `xmind/windows-import-root-utf8`
as `5e86144cce2730b06e0de11c155097cf71b84a58`. Its Release build, three focused
checks and all 11 default performance cases passed. Of 53 allowed SDK checks,
51 passed and two failures reproduced with the unchanged baseline. Five
CPython peer checks were excluded; no CPython interpreter or bridge was used.
Both the native workflow and runtime staging now select this exact SDK pin.

The following entries describe earlier checkpoint scopes; their pending
components are superseded only where the replacement validation above applies.

The durable retirement request and pre-migration startup refusal passed all
**95 local native contracts in 193.41 seconds**, with 622 inputs unchanged.
Actual SQLite commit rollback, a resume/retirement race, same-generation resume
rejection, observable closed admission and three schema 12 startup-refusal
fixtures passed. Qualified native controller and HTTP observation checks also
passed. The metadata-order publication error is retained with a successful
supplemental two-contract detail run. The earlier `c9335ca` hosted scoped-approval
timeout remains failed; the new diagnostic preserves its deadline and is not a
root-cause fix. Process shutdown, qualified replacement bootstrap, legacy
migration and installed VS Code writing remain pending.
[Retirement scope](native-owner-retirement.md),
[exact evidence and retained failure](evidence/native-owner-retirement-local.json).

The optional native owner controller/HTTP boundary passed all **95 local native
contracts in 190.08 seconds**, with 622 inputs unchanged. Actual HTTP mutation
races, scoped-view denial, authority/receipt checks and context/A2A admission
fencing passed against C++/embedded-xlang3 SQLite. The controller checks the
actual runtime and the qualified loaded package and rejects a mismatched
transport binding. Earlier package-fixture startup failures are retained.
Production server enablement, retirement/bootstrap, legacy migration and
installed VS Code writing remain pending.
[Owner commands and limits](native-owner-control.md),
[exact local evidence](evidence/native-owner-control-http-local.json).

The native runtime-package verifier passed all **94 local native contracts in
183.90 seconds**, with 618 inputs unchanged. Actual Windows image identity,
Unicode paths/names, inventory/hash checks and pinned-file protections passed.
It also verified the complete 1,861-file previously accepted `c42a5c4` bundle
without executing that candidate. This is a tested C++ component; production
controller wiring, native retirement/bootstrap, legacy migration and installed
VS Code writing remain pending.
[Package-verification scope](native-runtime-generation.md),
[exact local evidence](evidence/native-runtime-generation-local.json).

The native persistence owner barrier passed the complete **93-contract local
gate in 182.54 seconds**, with all 614 inputs unchanged. Queued and parallel
admission races, receipt checks, publication/deferred-commit failure rollback,
active run/context ownership, unresolved effect rejection, encrypted credential
retention and actual database reopen passed. This is the C++ persistence part
of runtime handoff. Authenticated transport controls, runtime health/idle
checks, retirement, target-generation admission and installed migration are
not implemented by this increment. The installed `TestProj` owner was observed
alive without approved edits; it remains read-only.
[Owner barrier and limits](native-backend-owner.md),
[exact gate and retained attempts](evidence/native-backend-owner-local.json).
The exact `d489b21` hosted native workflow subsequently completed successfully,
including its full gate, views, integration and packaging. Independent archive
verification and installed acceptance of that checkpoint remain pending.

The approval-based file-proposal source passed all **92 native contracts in
178.43 seconds** and the complete **192 extension / 39 browser contracts**.
Native health advertises its actual startup policy. New managed owners default
to approval-required file creation/editing; explicit read-only settings and
workspace trust still apply. Both views show the actual policy when advertised.
The initial 19 view-fixture failures are retained alongside the corrected full
gate. Native inputs remained frozen; the later fixture-only correction is
covered by the final view gate. These are local source checks, not installed
writing acceptance. The current normal-profile `TestProj` owner still runs its
older read-only startup policy; preserving its history through an upgrade
remains pending. [Policy evidence](evidence/native-file-proposal-policy-local.json).
The exact `ba3d109` isolated hosted 192/39 view gate also passed; independent
archive, pinned-source and generated-asset verification is complete.
[Hosted view verification](evidence/native-file-proposal-policy-views-hosted.json).

The latest owner-retention source passed all **189 extension and 38 browser
contracts locally**, with 42 inputs frozen and generated asset hashes retained. A package or
machine-setting change keeps the authenticated existing workspace backend and
reports pending changes; missing/unsafe/changed owners fail explicitly without a
replacement database. Existing owner records are not evicted at the 128 limit.
A separate actual packaged native/embedded-xlang3 SQLite API comparison retained
one process, origin, database and exact saved records across a package change and
adapter host reload. It used model-free fixtures, not an installed editor. Its
signal-exit cleanup flag is false due to a helper defect; the original receipt
and later independent absent-process/workspace observation remain recorded.
A fresh corrected receipt run deferred before execution on SDK benchmark PID
32512. A subsequent fresh run passed the real native/SQLite owner comparison
and observed exit before exact owned cleanup; both earlier failures remain
retained. [Corrected receipt](evidence/native-workspace-owner-continuity-cleanup-corrected.json).
The exact `a02cdb0` hosted native workflow subsequently completed successfully,
including packaging; independent verification of its archives and
installed/rendered retention remain pending. Native generation migration
remains unimplemented.
[Owner-retention evidence](evidence/native-workspace-owner-continuity-local.json).
Its exact isolated hosted 189/38 view archive and source/assets are independently
verified. [Hosted retention views](evidence/native-workspace-owner-continuity-views-hosted.json).

The latest native CLI skill controls passed all **92 local native contracts in
175.20 seconds**, with all 612 input hashes unchanged. Exact registered and passed
test names match the complete CI manifest. Unicode Windows arguments/snapshots,
stale selection rejection, concurrent clients and explicit interactive recovery
passed through the actual CLI and native backend. The fixtures use synthetic
guides/provider peers and admit no inference runs. The benchmark deferral and
earlier 91/92 diagnostic failure remain recorded.
[CLI evidence](evidence/native-cli-user-skills-local.json).

The preceding `c42a5c4` user-control source passed the complete hosted native
workflow, 184 extension tests, 38 browser tests, integration and packaging.
Independent archive/inventory/source checks verify that exact runtime and VSIX.
Its isolated hosted 184/38 view gate also passed. These packages have not replaced
the running previews. The newer CLI commands are absent from that package.
[Package verification](evidence/native-user-skill-controls-hosted-package.json),
[view verification](evidence/native-user-skill-controls-views-hosted.json).

The normal persistent VS Code profile still runs the installed `5d48bdd` package
against `TestProj`. After user-granted trust, it passed an actual OpenAI file read
and window reload with the same saved run, response, model and actual metrics.
The latest observation confirms that sidebar state and effective workspace.
[Trusted-workspace acceptance](evidence/native-vscode-trusted-workspace.json).
Rendered user skill controls and existing-profile upgrades remain incomplete:
the newer launcher retains the existing owner while the package change is
pending. [Required native handoff](native-runtime-handoff.md).

The paragraphs below retain their historical checkpoint scope; they do not
supersede these current source, package or installed-preview boundaries.

The provider/conversation catalogue recovery source passed a complete isolated
hosted gate: **175 extension and 35 browser tests**, with no failures/skips.
Its source and generated assets were verified against the exact commit and
advertised artifact digest. The tested browser asset is published without a
local build or backend restart. Rendered recovery and an updated installed VSIX
remain pending while the local SDK benchmark is live. Both failed hosted
attempts and the pre-fix regression summaries are retained.
[Catalogue recovery and limits](model-catalogue-recovery.md).

The subsequent compact-height CSS checkpoint passed 173 extension and 33
browser tests, then actual short-pane rendering, long-draft and expanded-context
checks. The 319 × 431 browser pane now retains 112 pixels for history, up from
16. The browser styles are live and the verified VSIX is installed in the
existing profile; its open window still needs a reload. Native inputs and run
records are unchanged. A separate concurrent provider/session selection
catalogue issue was found and remains pending.
[Compact sidebar scope](compact-sidebar.md).

The browser preview is now upgraded to source `60475f8` and the paired xlang3
SDK `ad8040f`, at its existing local addresses. Its live API records and cookie
were preserved; a separate disposable schema 10 → 12 → 10 fixture passed,
with its earlier failed negative probe retained. Fresh model-free native/browser
integration passed in 1,159 ms. Actual rendered Claude, OpenAI, Gemini and
DeepSeek runs completed with supplied token metrics. Refresh restored the
selected session/provider/model without a token prompt or new run. Desktop
right-sidebar layout passed; compact-height layout and rendered normal-profile
VS Code interaction remain pending.
[Browser checkpoint and limits](browser-preview-upgrade.md).

The latest model eligibility source passed **89/89 native contracts in 172.73
seconds**, with 564 inputs unchanged and no exclusions/skips. It adds exact
OpenAI wire/model capability declarations and preserves inactive Gemini text
profiles beside active coding profiles. Unchanged frontend inputs retain their
separate prior 173/33 results. New-source package and live acceptance remain
separate from the previous four-provider checkpoint below: source `60475f8`
passed real native reads/replies with all four configured providers, including
GPT-6.1 Sol, and returned 20 eligible OpenAI IDs. Its package passed independent
ZIP integrity and actual installed VS Code opened-folder binding. The ordinary
window needs a reload; rendered VS Code model interaction remains pending.
The later browser upgrade and rendered runs are recorded above.
[Current model policy and retained failures](native-model-eligibility.md).

Current source `d69a681886ebabcc38174985292fcd88dd7fc7ee` passed **88/88
native tests in 174.89 seconds**, **173 extension tests** and **33 browser
tests**. All four configured providers separately passed real native workspace
reads and exact replies. The packaged extension passed actual VS Code Open
Folder binding and was installed in the normal profile; existing-window reload
and folder trust remain user-visible prerequisites. At that checkpoint the old
browser preview was unchanged. The isolated xlang3 long-path branch is implemented and pushed, with
its known SDK baseline failure retained. Older paragraphs below describe their
original checkpoint and do not supersede these results.
[Current acceptance and limits](native-provider-workspace-acceptance.md).

The bounded native JSON POST foundation passed the complete guarded local
**78-contract gate in 193.91 seconds**, with all **427 frozen inputs** and
accepted xlang3 runtime files verified unchanged. The extended transport
contract took **8.80 seconds** and reached its exact 15 synthetic-peer POST
request assertions while preserving prior GET/SSE cases. The first attempt
deferred before native execution because an SDK benchmark was live; the later
attempt began with clear guards. Full per-test output was preserved before
CTest discovery. This implements transport; token-counting adapters, rolling
compaction and its controls remain pending. The installed preview is unchanged.
[Transport scope](native-context-transport.md),
[local evidence](evidence/native-context-transport-local-provenance.json),
[full compaction design](native-context-compaction-design.md).

Exact earlier source `2cd392f83f642ae5278e7d77fa55822258a8a126` passed its
[hosted gate](https://github.com/xlang-foundation/xMind/actions/runs/37824215372):
**78 native tests in 153.16 seconds**, **119 extension tests in 1.8119155
seconds** and **26 browser tests in 1.0266805 seconds**, all 16 steps plus
model-free integration and 18 VSIX assets. Source maps, advertised archive
digests and all extracted payloads were verified against exact Git input.
The raw job log and all 78 per-test blocks are preserved. This checkpoint
predates the JSON POST change and does not establish installed/live planning.
[Artifact-bound evidence](evidence/native-dynamic-mcp-hosted-2cd-provenance.json).

The dynamic coding-child MCP checkpoint passed the complete guarded local
**78 native contracts in 145.17 seconds**, with the exact complete manifest and
all **427 frozen native/build-source inputs** verified. The new contract took
**3.08 seconds**: five scenarios asserted 16 synthetic provider requests,
13 actual legacy-wire MCP discovery cycles and two actual MCP dispatches. Real
native coding-child ownership, a full nine-field immutable binding, exact
controller approval, peer filesystem writes, dependent native read verification
and embedded-xlang3 SQLite passed. Denial/cancellation had no peer effect;
child-only protocol drift rejected before its provider request; lost-reply
uncertainty retained the actual journal/node and did not replay after reopen.
The acknowledgement retains `independently_verified: false` despite the separate
read. This is not live/upstream-SDK/installed-IDE acceptance or a new-root
quarantine test. The first complete attempt passed **77/78 in 159.80 seconds**;
its fixture omitted required `depends_on: []`, and the sole peer correction
preserved production C++ and all assertions/counts. Both attempts remain separate.
[Local78 evidence and retained failure](evidence/native-dynamic-mcp-local-provenance.json).

The preceding hosted 3fc **77/119/26** results remain scoped below. UI sources
are unchanged; their suites were not rerun for this native contract. The managed
preview remains ace/schema v10, and the native path-gap discussion has not
resulted in an SDK fix or schema-v11 installation.

The schema-v11 preview upgrade remains **uninstalled**. Two disposable migration
attempts failed and are retained. A separately guarded diagnostic confirmed that
the exact xlang3 runtime reads an identical schema file at a 226-character Windows
path but fails to open its existing 288-character copy with `FileNotFoundError`,
before the ledger probe connects SQLite. No managed database, provider or SDK
source was changed. This is a native file-access compatibility finding, not a
passing migration/rollback result. The proposed branch fix is being discussed
under the user's requirement to discuss new native gaps first.
[Derived diagnostic summary and exact acceptance limits](evidence/native-dynamic-plan-preview-probe.json).

Exact source `3fc420480f61db75d3efaf4bda77fbefd8246166` subsequently passed its [hosted gate](https://github.com/xlang-foundation/xMind/actions/runs/37808089507): **77 native contracts in 206.89 seconds**, **119 extension tests in 2.4612639 seconds** and **26 browser tests in 1.3989153 seconds**. All 16 job steps passed, with the exact complete native manifest, zero failures/skips, model-free browser/native integration and 18 verified VSIX assets. Original logs, advertised archive digests and exact source/runtime maps were verified. Provider replies remain synthetic; this establishes the source checkpoint rather than installed or live planning acceptance. The installed preview remains ace/schema v10. [Artifact-bound hosted evidence](evidence/native-dynamic-plan-hosted-provenance.json).

The initial native dependency-planning checkpoint (schema v11) passed the complete guarded local **77-contract gate in 153.88 seconds**, with the exact expected/registered/passed manifest and all **421 frozen native source hashes** verified. Native contracts cover model-selected agent/human dependencies, an actual failed investigation, same-plan revision, clean pause/reopen, authenticated same-owner input, a separately approved coding-child edit and a dependent verification read. Provider replies, signatures and keys are synthetic; execution, files, permissions, transport and embedded-xlang3 SQLite are real. **119 extension and 26 browser contracts** passed at their separate renderer/controller scope. The earlier **68/77** and **76/77** gate failures remain separate retained attempts. [Initial implementation and limits](native-dynamic-plan.md), [local source and gate evidence](evidence/native-dynamic-plan-local-provenance.json).

Model-free browser/native graph/human/auth/cookie/reconnect integration passed against the rebuilt server and matching assets; all **18 required VSIX assets** were packaged and verified. These checks establish neither rendered dynamic-planning nor installed-editor acceptance. The installed preview remains **ace2460/schema v10** with the retained repaired browser view and its separately recorded live read-only join. Schema-v11 installation, live planning and installed VS Code acceptance remain pending. The initial 77-contract gates do not establish dynamic coding-child MCP peer effects; the
later local 78-contract contract above supplies that bounded actual-peer scope.
Direct MCP dynamic tool nodes now have a focused native approval/uncertainty
contract; process nodes, recursive planning, skills, context compaction,
outbound A2A and complete coding/provider parity remain required.

Native Responses reasoning continuation passed the exact ace2460 hosted gate: **71 native contracts in 171.52 seconds, 103 extension and 20 browser contracts**, with all 16 job steps, native/browser integration and 18 VSIX assets passing. Verified sources and artifact maps bind the completed-item continuation fix and its real native two-child read/join with synthetic provider replies. The local build was deferred by the benchmark guard. The exact native bundle is installed. One real OpenAI browser prompt completed two read-only native children and a joined parent response, with six owned model attempts, separate per-response metrics and zero effect operations. Immediate refresh retained the connection and selection. Upgrade and rollback checks passed at their separately documented scopes. [Compatibility and scope](native-responses-reasoning.md), [exact hosted evidence](evidence/native-responses-reasoning-hosted-provenance.json).

Ordinary Agent mode now offers native `delegate_tasks` for model-selected read-only workspace investigations. Actual child AgentRunners keep separate conversations and measured response usage/timing, share their parent's call/deadline limits, and return observed results before the parent continues or requests an approval-controlled effect. Schema v10 durably binds tasks, immutable presets, budgets and once-only settlements; recovery never replays children. CLI, browser and VS Code observe the same owned child histories and committed tree cursors.

For the preceding delegation checkpoint, the final guarded local **71-contract native gate passed in 142.36 seconds**, with an exact expected/registered/passed manifest, **45 frozen source hashes**, zero failures/skips and no exclusions. The delegation engine contract took **1.96 seconds**, actual HTTP/CLI/shared-controller acceptance **1.03 seconds**. **103 extension tests** and **19 browser tests**, fresh source-matched browser/native integration and 18-asset VSIX verification also passed. Provider replies, signatures and keys are explicitly synthetic; native agents, filesystem effects, permissions, xlang3 SQLite and transport are real. The [initial process-fixture positional-schema failure](evidence/native-delegation-initial-ctest.log) is retained separately; the corrected frozen source passed all 71 contracts.

Exact revision `e353a37799530a234a6fa13e51f61a5c52d3ae6a` subsequently passed its [hosted gate](https://github.com/xlang-foundation/xMind/actions/runs/37765666399): **71 native contracts in 175.51 seconds**, **103 extension tests in 2.9668103 seconds** and **19 browser tests in 1.426115 seconds**, plus native/browser integration and verification of all 18 required VSIX assets. The delegation engine took **4.88 seconds** and its HTTP/CLI contract **1.22 seconds**. All 16 job steps succeeded, with zero failures/skips and the exact complete native manifest. Downloaded artifact digests, exact source and runtime/stdlib pins were verified. [Exact hosted71 provenance](evidence/native-delegation-hosted-provenance.json), [raw hosted CTest](evidence/native-delegation-hosted-ctest.log), [original hosted job](evidence/native-delegation-passing-ci-job.log).

The preceding managed backend upgrade installed the exact hosted e353 bundle with schema v10; the later 7fe upgrade is recorded in the diagnostic scope. The actual upgrade preserved existing records, settings and browser access; its disposable schema 9→10→9 rollback fixture passed separately. The original e353 webpage exposed a classic-script name collision. A separate local browser repair passed **103 extension tests in 1.6557025 seconds**, **20 browser tests in 0.9090882 seconds**, native/browser integration and all 18 VSIX assets. The repaired page reused its cookie without key entry and restored prior history/metrics, the selected model and Agent mode. These frontend results are separate from the unchanged hosted 71/103/19 gate. [Upgrade evidence](evidence/native-delegation-upgrade-provenance.json), [acceptance scopes](native-delegation-acceptance.md).

The first e353 live `delegate_tasks` request failed with `responses_terminal_mismatch` before child admission: zero children, only the user history row and one parent model-budget attempt. That historical failure remains recorded; the later ace browser two-child join passed its separate acceptance above. Installed VS Code delegation acceptance remains unverified. The initial v11 planning source checkpoint is distinct from those installed/live results; broader planning, skills, compaction, outbound A2A and full provider/coding parity remain incomplete. [Implementation and limits](native-delegation.md), [broader native planning design](native-dynamic-plan-design.md).

The preceding direct-MCP checkpoint passed the complete local **69 native contracts in 121.22
seconds**, with the exact expected/registered/passed manifest, zero
failures/skips and no post-build exclusions. All **14 frozen source hashes**
match the final pre-build state. The direct MCP graph contract passed in **5.59
seconds**, and actual administrator/server/CLI/shared-view-adapter integration
in **2.31 seconds**. Fresh browser/native integration passed the rebuilt server
and source-matched assets. An initial HTTP startup failure exposed an obsolete
model-only guard; the corrected final source and expanded model-configuration
negatives passed the full gate. Its [failed interim log](evidence/native-graph-mcp-initial-ctest.log)
is retained separately and is not counted as a pass.
[Local69 source/runtime/log evidence](evidence/native-graph-mcp-local-provenance.json).

Exact `c4ec09fc3ee25c1b3a2bd9087b25f5af420ee616` subsequently passed its
[hosted gate](https://github.com/xlang-foundation/xMind/actions/runs/37755382937):
**69 native contracts in 184.36 seconds**, **98 extension tests in 3.2107402
seconds** and **17 browser tests in 1.5543002 seconds**, native/browser integration
and verification of all 18 required VSIX assets. All 16 job steps succeeded;
zero failures/skips and the exact expected/registered/passed manifest were
verified. The direct MCP graph and HTTP/CLI contracts took **12.60/3.87 seconds**.
All 14 final local source hashes match this hosted revision. Raw job bytes,
including the UTF-8 BOM, are preserved. Exact runtime/evidence archive digests,
source/runtime/standard-library pins and the SQLite patch hash were checked;
safe fresh extraction records all 29 runtime, 30 VSIX and 12 browser-runtime
file hashes. Downloaded artifact verification does not establish installation
or a live provider/IDE result.
[Exact hosted69 evidence](evidence/native-graph-mcp-hosted-provenance.json).

MCP nodes exclusively use an exact alias, a pinned enabled server revision and
a strict `arguments_json` object string. Native execution rediscovery precedes
approval, then the shared registry owns schema validation, exact controller
authorization, operation claims and outcomes. Root deadlines cover discovery,
permission waits and dispatch. Unknown/drifted aliases and invalid inputs
reject before `tools/call`; uncertain/unrecorded outcomes preserve quarantine or
fail-stop ownership without replay. Acknowledged output above 64 KiB fails the
graph child while retaining its succeeded operation and actual request IDs.
Literal number tokens and escaped keys remain exact. Referenced typed graph
values reject floats or integers outside ±9,007,199,254,740,991 before child
admission. Metadata/admission do not launch peers. Stale durable human pauses
remain inspectable/cancellable but resume rejects before input commit. The
stopped-backend administrator adds private credential resolution and catalogue
reflection rejection. Actual owned peers write real disposable files after
approval; dependent native reads, human pause/reopen, denied/cancelled/invalid/
drift/stale paths, jointly shared-workspace/configured-server claim serialization, lost replies, acknowledged
output bounds and injected outcome-journal failures/recovery passed through the
native graph/permission engines and embedded-xlang3 SQLite.
[Implementation and acceptance scope](native-graph-mcp.md).

At the direct-MCP checkpoint, frontend sources remained unchanged from the prior verified `ae7c4ba`/`fceb50b`
checkpoints, retaining **98 extension and 17 browser tests** in the local scope;
those suites were rerun and passed in the exact hosted69 gate above.
That verified c4 native bundle and packaged view were installed, with all
29/12 artifact hashes matched and prior records/configuration preserved. A
disposable actual upgrade and explicit rollback passed before installation.
One live read-only OpenAI Agent request, retained visible metrics and subsequent
rendered refresh passed. The original browser session needed one reconnect;
its precise cause remains unresolved. Installed native CLI `provider-profiles`,
`models` and the live acceptance `history` reads also passed without inference
or discovery. [Installed acceptance](preview-checkpoint-c4ec09fc.md),
[exact public record](evidence/live-browser-checkpoint-c4ec09fc.json).
This historical installed scope is superseded by the e353 backend upgrade and
separate browser repair recorded in [current acceptance](native-delegation-acceptance.md).
Synthetic peer protocol fixtures do not establish live direct-MCP graph or
rendered IDE acceptance, or complete MCP/graph parity.

The preceding `fceb50b` source passed the complete local **67 native contracts in 117.65
seconds**, with the exact expected manifest, zero failures/skips and no
post-build exclusions. The new Claude history helper passed in **0.50 seconds**;
the expanded actual Claude AgentRunner contract passed in **0.57 seconds**.
Ordered text/tool/thinking/redacted receipts keep raw input objects inside
`content_json` strings, retaining precise number tokens, whitespace and escaped
keys through agent-shaped JSON round trips and outgoing Messages replay.
Receipt validation checks visible text, ordered tool IDs/names/raw arguments,
native/provider finish mapping and same-wire assistant provenance.

Two real file reads run through interleaved signature-bearing blocks. The
encrypted provider credential and exact ordered receipt/history survive
xlang3 SQLite close/reopen and a new native run without repeating tools. A
signature-only final thought retains its signature alongside the visible answer;
hidden thinking, signatures and redacted data never become ordinary `model.text`.
Unsigned blocks fail before tools, while the prior malformed/unoffered/
incomplete/late-error/truncated turns, cancellation/recovery and SQL
conversation-batch rollback still pass.

Native content/envelope and final request bounds are **8 MiB** each; content is
limited to **64 blocks**, aggregate visible text to **4 MiB**, tool input to
**1 MiB/depth 16** and each signature to **65,536 bytes**. Request message/tool
counts, aggregate receipt budget and role merges are bounded before large
allocation or concatenation. Foreign, mismatched and empty outgoing receipts
fail before transport; signatures/redacted data remain structurally checked
opaque strings without local cryptographic verification.

Fresh browser/native integration passed the rebuilt server and matching assets
in disposable processes/database. Frontend source equality with `ae7c4ba` is
verified; its prior local **98 extension and 17 browser tests** are retained,
without rerunning those suites for this native change. Provider replies, keys
and signatures are synthetic. Thinking/reasoning request controls, live Claude
acceptance, foreign signed/tool conversion, actual rendered IDE acceptance and
installed-preview upgrades remain incomplete. Exact revision
`fceb50ba0493f645ee5f0a00d5400e0f103df231` subsequently passed its
[hosted gate](https://github.com/xlang-foundation/xMind/actions/runs/37752689263):
**67 native contracts in 120.18 seconds**, **98 extension tests in 2.0700124
seconds** and **17 browser tests in 0.9230928 seconds**, native/browser integration
and verification of an 18-asset VSIX, with zero failures/skips and the exact
manifest. Hosted history/agent contracts took **0.31/1.03 seconds**. This
separate gate excludes the current direct MCP graph, administrator and server
startup changes.
[Local67 source/runtime/log evidence](evidence/native-anthropic-history-local-provenance.json),
[exact hosted67 evidence](evidence/native-anthropic-history-hosted-provenance.json),
[Claude receipt scope](native-claude-history.md).

The preceding `ae7c4ba` source passed the complete local **66 native contracts in 114.49
seconds**, **98 extension and 17 browser tests**, with the exact expected native
manifest, zero failures/skips and no post-build exclusions. The new actual
Claude AgentRunner contract passed in **0.56 seconds**; its native provider
adapter contract passed in **0.23 seconds**. Two real native file reads and
correlated Messages tool IDs/results survive actual xlang3 SQLite close/reopen
and a new run's exact history replay without repeating earlier tools. Encrypted
provider credentials and terminal run ownership remain intact. Malformed,
unoffered, incomplete, length-truncated and late-error provider turns fail before
tool effects or successful assistant history. Held-stream cancellation and later
actual execution recover; injected tool-row SQL failure rolls back the
conversation batch while retaining observed real read outcomes.

Native Claude usage retains supplied raw input/output/cache counters and zeros,
adds common aliases without inventing totals, and marks input as uncached.
Actual shared DOM tests display live usage events and persisted transcript
metadata, separate cache writes/reads, safe zero counters, missing counts,
provider/model identity and backend-measured timing. Fresh browser/native
integration passed against the rebuilt server and source-matched assets using
disposable processes/database. The independent provider socket replies and
credentials are synthetic. This prior gate excludes the newer ordered
signed/redacted Claude receipt work above. Exact revision
`ae7c4ba07d59f57cbe393eff5a01b7d5186dfbc5` subsequently passed its
[hosted gate](https://github.com/xlang-foundation/xMind/actions/runs/37748603655):
**66 native contracts in 131.92 seconds**, **98 extension and 17 browser tests**,
native/browser integration and VSIX verification, with zero failures/skips and
the exact expected manifest. Its Claude agent contract passed in **1.41 seconds**.
This hosted result also excludes the newer ordered signed/redacted receipt work;
it is not hosted67 verification.
[Exact hosted66 provenance](evidence/native-anthropic-agent-hosted-provenance.json).
Live account, actual
rendered IDE and installed-preview acceptance are separate requirements.
[Exact local provenance](evidence/native-anthropic-agent-local-provenance.json),
[Claude agent scope](native-claude-agent.md).

The earlier `ac69c1f` CLI checkpoint separately passed the complete local
**65 native contracts in 113.16 seconds**, with its exact manifest, zero
failures/skips and no post-build exclusions. Its CLI contract took
**4.23 seconds**. It exercises
OpenAI, Claude and Gemini catalogue authentication, private environment-key
setup, saved-key updates, explicit profile selection and actual xlang3 SQLite
reopen with encrypted credentials. Signed Gemini text/history/usage, stale discovery/admission
without retry, safe native provider diagnostics and failed-turn status preserved
through settings until a later actual successful turn also passed. Public
profile/model key reflection is rejected across all three provider families.
Provider sockets and credentials are synthetic.
[Local CLI provenance](evidence/native-provider-profile-cli-local-provenance.json).
Exact revision `ac69c1f2a2c4a757b23d0d9da8b98d6188fab3a8` subsequently passed
its [hosted gate](https://github.com/xlang-foundation/xMind/actions/runs/37746664258):
**65 native contracts in 149.37 seconds**, **94 extension and 17 browser tests**,
native/browser integration and VSIX verification, with zero failures/skips and
the exact expected manifest. This hosted result excludes the newer Claude
agent/metrics and receipt milestones above.
[Hosted CLI provenance](evidence/native-provider-profile-cli-hosted-provenance.json).

At that CLI checkpoint, unchanged thin-client sources retained the earlier
verified **94 extension and 17 browser tests**, with zero failures/skips; those
suites were not rerun for the
CLI change. Fresh browser/native integration passed against that rebuilt server
and source-matched assets, including four default routes, inactive Gemini
profile save, refresh/restart, real file-reading graph execution, history and
reconnect. No live Gemini inference, rendered IDE acceptance or installed-preview
upgrade is claimed. [CLI scope](native-interactive-cli.md),
[provider setup](provider-setup.md).

Enrollment revision `2f5e0f05e9bfadcf86b5508863da0ec5a9e78cfe` passed its
exact hosted **64 native, 94 extension and 17 browser contracts**, native/browser
integration and VSIX verification, with zero failures/skips. Its native gate
took **158.09 seconds**. It previously passed the local 64-contract gate in
**120.98 seconds**. Gemini discovery/pagination, generation-method filtering
separate from native tool policy, encrypted enrollment/selection, owned-key file
execution, cancellation, CAS rollback and signed SQLite replay passed using
synthetic peers. Direct-SQLite removal and shared `records.hpp` cleanup are
included. This hosted result excludes the newer generic CLI controls and
all-provider reflection guard.
[Hosted enrollment run](https://github.com/xlang-foundation/xMind/actions/runs/37743203538),
[exact hosted provenance](evidence/native-gemini-enrollment-hosted-provenance.json),
[earlier local scope](evidence/native-gemini-enrollment-local-provenance.json).

Gateway/history revision `75f45f0a036f1ffbab8c4b157df364f3697b52a0` passed the
exact hosted gate: **61 native, 91 extension and 17 browser contracts**,
native/browser integration and VSIX verification, with zero failures or skips.
Its native gate took **121.84 seconds**.
[Hosted run](https://github.com/xlang-foundation/xMind/actions/runs/37738970918),
[exact provenance](evidence/native-gemini-history-hosted-provenance.json).
This includes common Gemini gateway/history receipts, separate native/provider
IDs, normalized supplied usage, strengthened callback ordering/argument/usage
assertions, aligned replay limits and escaped large-response regressions. It
excludes later `c9591fe` agent acceptance and new catalogue/profile enrollment.

The cleanup revision `dea588874e5a8c8e40bf1a158fa925520443c8a0` passed its
hosted gate: **58 native, 88 extension and 17 browser contracts**, native/browser
integration and VSIX verification, with zero failures/skips.
[Hosted run](https://github.com/xlang-foundation/xMind/actions/runs/37734970144),
[exact provenance](evidence/native-cleanup-hosted-provenance.json).
This verifies removal of the prototype/legacy tree, the native root build,
merged `doc/`, descriptor attribution, graph dependency filtering and the original
Gemini request component. It also verifies the repository error-boundary fix
that failed the earlier request gate.

Gemini transport/replay revision `ddd1d3da087f9ca7f8b0b8d81705c82632e15094`
also passed its separate hosted gate: **60 native, 88 extension and 17 browser
contracts**, native/browser integration and VSIX verification, with no
failures/skips. [Hosted run](https://github.com/xlang-foundation/xMind/actions/runs/37736323072),
[exact evidence](evidence/native-gemini-transport-hosted-provenance.json).
This verifies native request/SSE/HTTP components and expanded replay data;
it excludes the newer common gateway, history bridge and limit fixes below.

The subsequent `c9591fe` source passed **62 native contracts** locally in
**100.44 seconds**.
It includes Gemini SSE/HTTP, expanded replay data, common model gateway, history
bridge, stronger callback ordering/usage assertions and escaped large-read
response-limit regressions. The new native agent contract executes two actual
file reads, closes/reopens xlang3 SQLite, replays exact signed receipts/results
through a new run, and rejects malformed/truncated/unoffered calls without effects.
All cases compiled and passed; there are no post-build exclusions in this gate.
The earlier local 61-contract log retains its historical exclusions; the hosted
`75f45f0` gate above includes the later callback and large-response assertions.
[Local agent provenance](evidence/native-gemini-agent-local-provenance.json).

At that 62-contract milestone, thin-client sources matched the earlier **91
extension and 17 browser** pass; source equality is recorded. Actual browser/native
integration passed again against its compiled server with disposable
processes/database. Exact revision `c9591fe79cad9a4253ac8088933f0c8a2848ded1`
subsequently passed its hosted **62 native, 91 extension and 17 browser
contracts**, native/browser integration and VSIX verification, with no
failures/skips. Its native gate took **128.80 seconds**.
[Hosted run](https://github.com/xlang-foundation/xMind/actions/runs/37740505866),
[exact agent provenance](evidence/native-gemini-agent-hosted-provenance.json).
Neither the earlier hosted 61 gate nor the local/hosted 62 gates cover new
catalogue/enrollment source.

The [Gemini bridge](native-gemini-history.md) uses labelled synthetic component
and socket fixtures. Actual native AgentRunner/file/SQLite acceptance is now
verified with synthetic provider replies, including the new native
discovery/enrollment source in the local/hosted 64-contract gates and the generic
CLI boundary in the separately verified local/hosted 65-contract gates. The
prior local 66-contract gate verifies actual Claude AgentRunner/file/SQLite
acceptance and its shared metrics renderer at the scoped synthetic-provider/DOM
boundary; the separate local/hosted `fceb50b` 67-contract gates additionally verify ordered
raw/signed/redacted receipts and same-wire replay without repeated effects.
Live Gemini and Claude inference remain
unverified.

The preceding `c4ec09fc` installed browser preview's records and one live OpenAI read-only scenario were
verified separately from the hosted tests. See
[historical installed scope](preview-checkpoint-c4ec09fc.md). The preceding e353
backend, separately repaired browser and first live delegation failure are
recorded in [current acceptance](native-delegation-acceptance.md).

The Python prototype and original xlang runtime/service/plugin assets, launchers
and dependent probes are removed. Both root and Native CMake entry points use
the current native contracts. The unused FastAPI setup, lockfile, dependency
probe and prototype HTTP MCP peer were also removed. [Cleanup](cleanup.md).
The final 64-contract build also removes the unused direct-SQLite store and
contract; shared record types now live in `records.hpp`. Production database I/O
continues through embedded xlang3.

Complete coding parity, broad native providers, remaining MCP/A2A capabilities
and Local/Nexus connection profiles remain active requirements. Team-server
implementation, PostgreSQL, WebRTC and Electron belong to private Nexus.
[Architecture](architecture.md), [parity baseline](PARITY.md).

The current local delegation-guidance checkpoint (based on `808d515`) built
`agentflow_native_delegation_contract` and the full `xmind` Debug executable
with Visual Studio 18 CMake. The focused CTest run passed
`native_delegation_contract` (1/1). It verifies that model-facing delegation
guidance recommends direct handling for small/sequential edits, concise scoped
child findings, and warns that child calls share the parent model-call and
wall-clock budgets. A fresh hash-verified development runtime for exact xMind
revision `bc2d222a9e2a2f5fe04858fa81deadfddb9de0ce` was then launched with an
isolated local profile and browser on `127.0.0.1:57746`, using the configured
saved Claude profile. The UI fetched 13 Claude models and a real
`claude-sonnet-4-6` request to explain `hello.py` completed in the parent tool
loop: one `read_file`, zero delegated children, and no write or process
operations. The provider reported two model attempts, each with uncached input
and output metrics, and the run completed successfully. This verifies direct
handling for a small read-only task on the rebuilt executable. A second live
run on that same runtime tested an approved one-file coding task in the
disposable `D:\CantorAI2026\xMindLiveCodingSmoke` workspace. Run
`84c89a071a295fc41c3bc29bbacae215` used only `read_file` and `edit_file`, had
zero children, and completed the approved `replace_file` operation. A separate
disk check confirmed the requested description and Open Graph tags were
written while the page title and body stayed intact. These verify the direct
tool loop for a read-only task and an approved edit with the rebuilt executable;
they do not prove behavior across larger tasks or other providers. The edit is
an isolated HTML smoke, not a replacement for the earlier TestProj Vite build
or broad coding/IDE acceptance.
