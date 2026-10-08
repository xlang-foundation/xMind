# Reviewable milestones

Current completion scope is the [revised xMind OSS specification](architecture.md). Team-server features, PostgreSQL, WebRTC and the standalone Electron IDE are excluded and reserved for Nexus. Historical checkpoint paragraphs below preserve their original source/time scope and do not reinstate those requirements.

## Current checkpoint scope

The current Claude receipt checkpoint passed the full local **67-contract native
gate in 117.65 seconds**, with an exact manifest, zero failures/skips and no
post-build exclusions. The new history helper passed in **0.50 seconds**; the
expanded actual AgentRunner contract passed in **0.57 seconds**. Ordered
text/tool/thinking/redacted blocks, exact initial/fragmented tool-input JSON,
precise number tokens and escaped keys survive validated receipt persistence
and raw outgoing Messages replay. Two real file reads execute through
interleaved signature-bearing blocks; encrypted credentials and ordered history
survive xlang3 SQLite close/reopen without repeating tools. Unsigned blocks fail
before effects, while previous protocol failure, cancellation/recovery and SQL
conversation rollback cases still pass.

Hidden thinking/signatures/redacted data remain continuation material rather
than ordinary text output. Native matching/provenance checks and request,
receipt, message-count, depth and aggregate/role-merge bounds passed: 8 MiB
content/envelope and final request limits, 64 content blocks, 1 MiB/depth16 tool
input and 65,536-byte signatures. Fresh browser/native integration passed the
rebuilt server and matching assets. Frontend source equality with `ae7c4ba`
retains its prior **98 extension and 17 browser tests**, without a new frontend
rerun. Provider replies/keys/signatures are synthetic; local cryptographic
verification is not performed. Thinking/reasoning request controls, live Claude
acceptance, foreign signed/tool conversion, rendered IDE acceptance, preview
upgrades and complete parity remain incomplete. Exact hosted67 verification
remains pending.
[Local67 evidence](evidence/native-anthropic-history-local-provenance.json),
[receipt scope and bounds](native-claude-history.md).

The preceding `ae7c4ba` Claude checkpoint passed the full local **66-contract native gate
in 114.49 seconds**, with an exact manifest, zero failures/skips and no post-build
exclusions. Its new actual AgentRunner contract took **0.56 seconds**, verifying
two native file reads, correlated tool-use/results, encrypted credential reuse,
xlang3 SQLite history/reopen without repeated tools, held-stream cancellation
and recovery, and second-tool-row SQL rollback. Its normalized usage adapter
contract passed in **0.23 seconds**. Supplied uncached input/output/cache counters
and zeros remain distinct in events and persisted responses, without sums or
invented totals.

Frontend suites passed **98 extension and 17 browser tests**, with zero
failures/skips and actual shared-renderer DOM metric fixtures. Fresh browser/native
integration passed against that rebuilt server and matching assets. Exact
revision `ae7c4ba07d59f57cbe393eff5a01b7d5186dfbc5` subsequently passed its
[hosted gate](https://github.com/xlang-foundation/xMind/actions/runs/37748603655):
**66 native contracts in 131.92 seconds**, **98 extension and 17 browser tests**,
native/browser integration and VSIX verification, with zero failures/skips and
an exact manifest. Its Claude agent contract passed in **1.41 seconds**.
Both prior gates exclude the newer ordered signed/redacted receipt work above;
this is separate from hosted67 verification, CLI65 and hosted enrollment64.
Live Claude inference, rendered Claude IDE acceptance, the 21 pinned models and
full parity remain incomplete. Installed previews are unchanged.
[Local66 source/runtime/log evidence](evidence/native-anthropic-agent-local-provenance.json),
[exact hosted66 evidence](evidence/native-anthropic-agent-hosted-provenance.json),
[Claude scope](native-claude-agent.md).

The user-requested native-only cleanup was committed and pushed as
`dea588874e5a8c8e40bf1a158fa925520443c8a0`. The Python agent prototype and
old xlang Core/service/plugin assets are removed, root CMake delegates to
`Native/`, and maintained documentation is merged into `doc/`. Its hosted gate
passed **58 native, 88 extension and 17 browser contracts**, native/browser
integration and VSIX verification, with no failures/skips.
[Exact cleanup evidence](evidence/native-cleanup-hosted-provenance.json).

Gemini transport/replay source
`ddd1d3da087f9ca7f8b0b8d81705c82632e15094` passed a separate hosted gate:
**60 native, 88 extension and 17 browser contracts**, native/browser integration
and VSIX verification, with no failures/skips. This verifies native request,
SSE/HTTP transport and signed replay DTOs with synthetic socket fixtures.
[Exact transport evidence](evidence/native-gemini-transport-hosted-provenance.json).

The common gateway/history source
`75f45f0a036f1ffbab8c4b157df364f3697b52a0` passed its exact hosted gate:
**61 native, 91 extension and 17 browser contracts**, native/browser integration
and VSIX verification, with zero failures/skips. Its native gate took **121.84
seconds** and includes the stronger callbacks and escaped large-response cases
excluded from the earlier historical local 61 result.
[Exact hosted evidence](evidence/native-gemini-history-hosted-provenance.json).

The agent source `c9591fe79cad9a4253ac8088933f0c8a2848ded1` passed **62 native
contracts locally in 100.44 seconds**. Its real native agent/file reads,
xlang3 SQLite reopen and signed replay do not repeat prior tools. Provider
replies are synthetic. Browser/native integration passed again against its
compiled server, and its unchanged thin clients matched the earlier 91/17 pass.
[Local agent evidence](evidence/native-gemini-agent-local-provenance.json).
The exact hosted gate also passed **62 native, 91 extension and 17 browser
contracts**, native/browser integration and VSIX verification, with zero
failures/skips. Its native gate took **128.80 seconds** and excludes newer
catalogue/enrollment source.
[Exact hosted agent evidence](evidence/native-gemini-agent-hosted-provenance.json).

Enrollment source `2f5e0f05e9bfadcf86b5508863da0ec5a9e78cfe` passed its
exact hosted **64 native, 94 extension and 17 browser contracts**, native/browser
integration and VSIX verification, with zero failures/skips. The native gate
took **158.09 seconds**, following the local 64-contract pass in **120.98
seconds**. Authenticated Gemini pagination/full resources, encrypted enrollment,
owned-key file execution, cancellation/CAS failure, stale discovery and signed
xlang3 SQLite replay passed with synthetic replies, keeping generation methods
separate from backend tool policy. Direct-SQLite removal and shared `records.hpp`
cleanup are included.
[Exact hosted enrollment evidence](evidence/native-gemini-enrollment-hosted-provenance.json).

The preceding CLI checkpoint passed all **65 native contracts locally in 113.16
seconds**, with the exact manifest, zero failures/skips and no post-build
exclusions. Its new CLI contract took **4.23 seconds** and covers OpenAI, Claude
and Gemini profile catalogue/setup/selection, private environment input,
all-family public identity reflection rejection, safe provider diagnostics,
signed Gemini text/history/usage and actual SQLite reopen with encrypted credentials. Stale
discovery/admission does not retry or silently rebind; failed-turn status survives
settings success and recovers only through a later actual successful turn.
Fresh browser/native integration passed again against the rebuilt server and
source-matched assets. That checkpoint's unchanged frontend sources retained the earlier verified
**94 extension and 17 browser tests**, without rerunning those suites for the
CLI change. [Local CLI evidence](evidence/native-provider-profile-cli-local-provenance.json).
Exact revision `ac69c1f2a2c4a757b23d0d9da8b98d6188fab3a8` subsequently passed
its [hosted gate](https://github.com/xlang-foundation/xMind/actions/runs/37746664258):
**65 native contracts in 149.37 seconds**, **94 extension and 17 browser tests**,
native/browser integration and VSIX verification, with zero failures/skips and
the exact expected manifest.
[Hosted CLI evidence](evidence/native-provider-profile-cli-hosted-provenance.json).
That hosted result excludes the newer Claude agent/metrics and ordered-receipt
milestones. Installed
previews are unchanged; live Gemini inference, actual Gemini IDE acceptance and
full provider/coding/protocol parity remain incomplete.
[Provider setup](provider-setup.md), [CLI scope](native-interactive-cli.md).

The installed browser preview is still the separately verified native
`19d69dd` and view `6f32d215` pair. These source checkpoints do not imply a
preview upgrade. [Current validation](VALIDATION_STATUS.md),
[installed provider scope](provider-setup.md).

All counts describe bounded verification, not product or OpenCode parity
percentages. Broad native providers, full coding workflows, remaining MCP/A2A,
graph behavior and Local/Nexus connection contracts remain active work.

## Historical native milestone records

The records below retain their original revision/time and fixture scope. Words
such as "current", "now", "pending" and installed-preview descriptions inside
these records apply to that historical checkpoint; the summary above is the
current source/verification status. No removed prototype is a supported runtime
or delivery requirement.

Native Responses provider checkpoint: explicit C++ wire selection now reaches the shared single-agent and graph engine, preserving stateless reasoning/tool continuation, actual supplied token metrics and persisted conversation replay. Two dependent agents perform actual file reads; provider-native items stay in child conversations rather than graph dependency/join outputs. Final local Release validation passed **52 native and 60 extension contracts**, no skips. [Scope and evidence](native-responses-provider.md). Inference/opaque reasoning are synthetic; live provider/IDE acceptance, interactive wire enrollment, standalone browser UI, broader provider coverage and full product parity remain pending.

The earlier native A2A v1 source `d07c61c725c3a1a4bf9ac85a2c6782575e52d8df` passed hosted **50 native and 60 extension contracts**, no skips, in [run 37693026697](https://github.com/xlang-foundation/xMind/actions/runs/37693026697). [Original full hosted job](evidence/native-a2a-v1-hosted-job.log). This result applies to that source, not the newer history/Responses changes or installed UI runtimes.

Native A2A history fidelity: protocol histories now preserve the admitted text-part boundaries, empty parts, and message/part metadata across both wire versions, completed stream replay and restart, while keeping model prompts separate and task histories isolated. Final local verification passed **50 native and 60 extension contracts**. The older flattening assertion's failure and its replacement checks are documented in [scope and evidence](native-a2a-v1.md). Hosted validation and preview installation of this source remain pending; full protocol/product parity remains incomplete.

Native A2A 1.0 core RPC checkpoint: direct versioned discovery/send/get/list/cancel/stream/subscribe now use the shared C++ executor and xlang3 repository, with default blocking semantics, cross-version durable retries and measured status times/cursor listing. The official SDK **1.3.0** exercised the native v1 wire without compatibility translation. Final local validation passed **50 native and 60 extension contracts**. [Scope, migration behavior and evidence](native-a2a-v1.md). Full protocol/product parity remains incomplete; hosted verification of this source and preview installation are pending.

Earlier native A2A discovery/streaming: authenticated discovery, Task-first SSE, persisted partial text/refusal, reconnect without reexecution and bounded viewer capacity now use the shared C++ executor and xlang3 journal. An official SDK **1.3.0** peer passed explicit v0.3 compatibility discovery/stream/get checks. The final isolated Release build passed **48 native and 60 extension contracts**. The hosted graph pause/shutdown failure is preserved and its ownership race is fixed with repeated regression cases. [Scope and evidence](native-a2a-streaming.md). Hosted verification at `b13ea224b026b9b750f43bdd311254ca7855b53c` passed all **48 native and 60 extension contracts** in [run 37689748037](https://github.com/xlang-foundation/xMind/actions/runs/37689748037). The newer checkpoint above adds native v1 and blocking; continuation, remote delegation and full platform parity remain incomplete. Previews retain their current runtimes.

Earlier native A2A message admission: nonblocking text requests now use real C++ agent admission and xlang3 persistence, with atomic durable retry identities and task-local histories. The isolated Release build passed **47 native and 60 extension contracts**, including rollback/migration and actual server/worker transport with explicitly synthetic provider replies. [Scope and evidence](native-a2a-message-admission.md). At that checkpoint discovery and streaming were incomplete; the newer milestone above adds these components. Remote delegation and full platform parity remain incomplete.

Earlier native A2A task-control component: the C++ `/a2a` adapter reads actual shared tasks/artifacts and requests owner-controlled cancellation, preserving accurate observed states and rejecting internal child IDs. Final local Release validation passed **45 native and 60 extension contracts**, including actual graph/file/SQLite task execution and repeated finish/cancel scheduling. [Scope and evidence](native-a2a-task-control.md). At that checkpoint, admission and task-local history were incomplete. The newer milestone above adds those components; the subsequent streaming checkpoint adds discovery and scoped SDK interoperability. Remote/full protocol interoperability remains incomplete. This is not full A2A compliance or an installed-preview claim.

Current graph UI checkpoint: the actual right sidebar submitted `read.repository.file`, paused for human input, read the real repository `README.md`, and displayed matching bytes. Its completed root and transcript survived an editor reload. The isolated `graph-ui` backend/profile preserves the user's existing model preview and open Settings dialog. Join output is compact and expandable, and workflow choice persists per backend. **60 local extension contracts** pass. [Actual IDE scope and evidence](vscode-graph-workflows.md). Agent graphs with live inference/metrics and the remaining full product scope are still incomplete.

The native graph ownership/HTTP/CLI checkpoint passed 44 native contracts locally, followed by seven final access regressions; the sidebar's production client passed against compiled C++/xlang3 persistence. [Backend commands and scope](native-graph-service.md). The newer hosted builds must be reported from their exact GitHub run outcomes; these local checks do not establish hosted success. Earlier milestone paragraphs below retain their original source/time scope.

Automatic account model discovery: installed backend `5815141dcf27652a1af503e39f17127ba189c403` passed **43 native and 47 extension contracts** in [isolated CI](https://github.com/xlang-foundation/xMind/actions/runs/37665810113). [CTest](evidence/native-model-discovery-saved-key-hosted-ctest.log), [original job/TAP](evidence/native-model-discovery-saved-key-passing-ci-job.log), [provenance](evidence/native-model-discovery-saved-key-hosted-provenance.json). Manual ID input is replaced by actual API discovery and a searchable picker. Encrypted saved keys can be reused without another password, and legacy credential-as-model records no longer advertise a key or enable inference. The native preview was upgraded with its database/profile/draft preserved. Its actual account discovery was rejected by OpenAI for authentication/authorization; the actual private key replacement prompt is open. The adapter-only replacement action passed **48 local extension contracts**, separately from the hosted backend checkpoint. [Launch/local adapter verification](evidence/vscode-model-discovery-launch.json), [scope](provider-setup.md). Successful account listing and live provider inference remain pending.

Provider setup installed: source `86ff065cafeb0b43f9bc47adb2d16b631dacade3` passed **42 native and 44 extension contracts** in [isolated CI](https://github.com/xlang-foundation/xMind/actions/runs/37657184396). [Hosted CTest](evidence/native-provider-setup-hosted-ctest.log), [original job/TAP](evidence/native-provider-setup-passing-ci-job.log), [provenance](evidence/native-provider-setup-hosted-provenance.json). The actual preview now runs this bundle and opened its native model setup prompt, preserving the database/profile and user's draft. [Launch evidence](evidence/vscode-provider-setup-launch.json), [scope](provider-setup.md). The native contract's provider replies/key are synthetic; entering the user's key and receiving an actual provider response remain pending. Subsequent CLI enrollment source is still under verification.

Graph executor source `77ffa610661abdab910b270e086c1bedf9f2a325` now passed **41 native and 41 extension contracts** in [isolated CI](https://github.com/xlang-foundation/xMind/actions/runs/37655834894). [Hosted CTest](evidence/native-graph-runner-hosted-ctest.log), [original job/TAP](evidence/native-graph-runner-passing-ci-job.log), [provenance](evidence/native-graph-runner-hosted-provenance.json). This proves the native component contracts with synthetic inference; public graph service/client workflows remain pending. The subsequent provider-setup source is still under exact-revision native verification, and the installed preview remains model-free.

Provider setup source checkpoint: native authenticated model/key enrollment, encrypted xlang3-backed credential references, idle-service replacement and persisted reopen are prepared with a bottom-composer setup control. All **44 extension contracts pass locally**, including private password/dismissal/late-grant fixtures. [Adapter output](evidence/vscode-provider-setup-local.log), [implementation and verification limits](provider-setup.md). Native compilation/integration remain pending behind the observed local xlang3 benchmark guard and isolated CI. The current preview has not been upgraded; no live chat or provider acceptance is claimed.

Native graph executor component: schema-v7 immutable task input, bounded parallel agent/tool dispatch, typed dependencies, conditional skips and durable human pause/resume now use actual backend workers. Release compilation and all **41 native contracts passed locally**. The graph contract exercises real file reads/creation, separate approvals, cancellation and injected outcome-journal failure/recovery without replay; provider replies remain synthetic. [Complete local build/CTest output](evidence/native-graph-runner-local-build-ctest.log), [source/binary provenance](evidence/native-graph-runner-local-provenance.json), [component and remaining scope](native-graph-runner.md). Public graph service/HTTP/CLI/view workflows and live-provider acceptance remain pending. The local sidebar launcher now accepts explicit model/endpoint/workspace settings and passes the provider key only to the native encrypted credential store; sidebar provider setup and a live response remain unverified while the user's key location is pending. All **41 extension contracts pass locally**: [output](evidence/vscode-model-startup-local.log).

Earlier durable checkpoint source `b9e96fceb8a8cc25db597f51b2a50583294b6009` passed **40 native and 41 extension contracts** in [isolated CI](https://github.com/xlang-foundation/xMind/actions/runs/37653997518). [Hosted CTest](evidence/native-graph-checkpoint-hosted-ctest.log), [original job/TAP](evidence/native-graph-checkpoint-passing-ci-job.log), [provenance](evidence/native-graph-checkpoint-hosted-provenance.json). The installed model-free preview remains on its earlier verified foreground-process bundle.

Durable graph coordinator checkpoint: schema-v6 admission, observed-child settlement, human wait/input and conditional skips commit their checkpoint/event updates together. All **40 native contracts passed locally**, including actual SQLite revision conflicts, event-fault rollback and pause/reopen/resume. [Complete output](evidence/native-graph-checkpoint-local-build-ctest.log), [provenance](evidence/native-graph-checkpoint-local-provenance.json), [scope](native-graph-checkpoints.md). Expanded hosted CI and public graph scheduling/client acceptance remain pending.

Earlier child-execution source `fbf2cfe9962c6d364893090e4590f765f92b61a3` passed **39 native and 41 extension contracts** in [isolated CI](https://github.com/xlang-foundation/xMind/actions/runs/37651959372). [Hosted CTest](evidence/native-graph-children-hosted-ctest.log), [original job/TAP](evidence/native-graph-children-passing-ci-job.log), [provenance](evidence/native-graph-children-hosted-provenance.json). Inference is synthetic; installed preview remains on its earlier verified bundle.

Native graph child checkpoint: schema-v5 parent/child records and private conversations pass all **39 native contracts locally**, including concurrent shared-engine child requests, actual file reads, atomic admission rollback, root retirement guards, identified event replay and reopen/recovery. Inference is synthetic; public graph scheduling and client workflows remain pending. [Complete output](evidence/native-graph-children-local-build-ctest.log), [provenance](evidence/native-graph-children-local-provenance.json), [boundaries](native-graph-children.md). Expanded hosted CI is pending.

Native graph foundation checkpoint: Release compilation and all **38 native contracts passed locally**, including dependency joins, typed conditions/references, human-wait checkpoint restore, interrupted-node uncertainty/non-replay, actual xlang3/SQLite catalog revisions/faults/reopen and authenticated native admin/server ownership. Coordinator outputs are synthetic; actual agent/tool graph execution and client workflows remain pending. [Complete output](evidence/native-graph-foundation-local-build-ctest.log), [provenance](evidence/native-graph-foundation-local-provenance.json), [component and integration contract](native-graph-foundation.md). Expanded hosted CI is pending.

Native repository guidance approval checkpoint: Release compilation and all **37 native and 41 extension contracts passed locally**. Durable edit/create/command proposals bind observed guidance sources; after approval, C++ rechecks before dispatch and retires mismatches without an effect. Actual changed/added/removed source contracts require a separate second approval, while sidebar contracts show recorded source hashes and reject malformed review data. [Native output](evidence/native-guidance-approval-local-build-ctest.log), [extension output](evidence/vscode-guidance-approval-local.log), [provenance](evidence/native-guidance-approval-local-provenance.json), [boundaries](native-repository-instructions.md). Expanded hosted CI and live/populated view acceptance remain pending.

Earlier repository discovery source `5122d6a1842ea92c0704fe297386c3a80622b881` passed **37 native and 40 extension contracts** in [isolated CI](https://github.com/xlang-foundation/xMind/actions/runs/37644164701). [Hosted CTest](evidence/native-repository-instructions-hosted-ctest.log), [original full job/TAP](evidence/native-repository-instructions-passing-ci-job.log), [hosted provenance](evidence/native-repository-instructions-hosted-provenance.json). Inference is synthetic; the installed backend preview remains on its earlier foreground-process bundle.

Native instruction configuration checkpoint: source `61a5d692d7d205df0d7f5a630f307c529e14dc15` passed **36 native and 40 extension contracts** in [isolated CI](https://github.com/xlang-foundation/xMind/actions/runs/37642796445). Actual xlang3/SQLite revisions/faults/reopen, admin/server/model/CLI delivery, immutable owner lease and continued native permissions are verified with synthetic inference. [Hosted CTest](evidence/native-agent-instructions-hosted-ctest.log), [original full job/TAP output](evidence/native-agent-instructions-passing-ci-job.log), [provenance](evidence/native-agent-instructions-hosted-provenance.json), [component limits](native-agent-instructions.md). The installed preview remains on the earlier foreground-process bundle.

Verified native foreground process checkpoint: source `3297a4bce2d591c0f1c1dd37ccc18a15297369e7` passed **35 native and 40 extension contracts** in [isolated CI](https://github.com/xlang-foundation/xMind/actions/runs/37635775031). Actual child/file effects cover descendant timeout/cancellation, executable binding, approved/denied/stale commands, bounded binary output, SQLite journal failures/reopen, retained-output cursor replay and native CLI watcher disconnect/reconnect. Inference is synthetic. [CTest](evidence/native-process-passing-ci-ctest.log), [full job/TAP log](evidence/native-process-passing-ci-job.log), [provenance](evidence/native-process-passing-ci-provenance.json). The actual [unseeded right-sidebar preview](evidence/vscode-native-process-bundle-sidebar.png) is running the tested bundle with its database/profile preserved. No live-model/populated coding claim follows; full shell/background/PTY, protocol/graph/provider/team/Electron scope remains required. Earlier source-prepared and failed records below remain historical.

Durable foreground output source checkpoint: the native executor journals a bounded byte-exact output prefix through embedded xlang3; the sidebar reconstructs independent stdout/stderr with explicit missing-sequence/binary handling. All **39 extension contracts passed locally** with labelled fixtures: [complete output](evidence/vscode-process-stream.log). Actual pre-exit output, SQLite replay/write failures and HTTP/CLI restart contracts are expanded in source and await exact-revision CI. The parent-exit cleanup correction remains included. There is no passing expanded process bundle or populated/live-provider editor acceptance yet. [Implementation and limits](native-process-tools.md).

The user requested visible milestones. Show a runnable result, validation evidence and remaining scope at each milestone. Prepared source or a diagram alone does not prove a runnable native milestone.

Next coding component: the native foreground process adapter, durable approval executor, executable binding, persisted profile administration and model/server/CLI source are prepared, with 35 native contracts expected by isolated CI. Local compilation was deferred by a currently observed xlang3 timing controller; no process pass or verified model/server/editor integration is claimed. [Implementation/acceptance contract](native-process-tools.md). The latest verified runnable product remains the 31-native/30-extension creation checkpoint below.

Sidebar command review/output contracts now pass all **34 local extension tests**, including literal argument review, escaped/control/hex output, actual supplied exit/timing/count fields, malformed/expired proposal rejection and uncertain-command handling: [complete output](evidence/vscode-process-review.log). These are labeled DOM/host fixtures, not a populated command run in the interactive preview. Native process profile configuration and end-to-end model/server/editor validation remain required.

## M1: persistence through embedded xlang3 — verified

Verified supporting component: native Windows credential protection compiled and passed its synthetic binary/context/tamper/ownership test. It is now connected to SQLite through embedded xlang3; see [credential-storage.md](credential-storage.md). The historical native test log `build/native/evidence/contracts-20261006.log` preserves the passing protection test and the two initial failing SQLite tests; the later passing logs below supersede those failures.

Release compilation and native seed/read demo passed on October 6, 2026 after the user-authorized SQLite module fixes on `agentflow/mcp-native-compat`. Seed persisted a session, two messages, agent configuration and four ordered events. Independent native read processes reopened that state; `-After 2` returned only events 3 and 4. All three default native contracts passed ten consecutive runs. Evidence: `build/native/evidence/contracts-after-sqlite-fix-20261006.log` and `contracts-repeat-after-sqlite-fix-20261006.log`. Earlier failure evidence remains in `contracts-20261006.log`.

The xlang3 legacy SQLite fixtures and the new isolation/text fixture also passed. This milestone proves initial native persistence contracts, not the agent engine, HTTP service, remote storage or UI. Encrypted SQLite credentials passed the following repository checkpoint. The other checkout's 97-case benchmark process exited before the module build; recheck processes before subsequent heavy builds.

C++ demo and contracts are prepared. The source creates a session, two conversation messages, a completed run, durable events and a general agent-configuration record, using only xlang3 for SQLite operations. A second process reopens the database and reads history/events; a cursor limits replay. Existing sessions are not overwritten by seeding. Encrypted secrets are not implemented by this demo.

On an idle machine:

```powershell
.\Tools\native-milestone.ps1 -Action Build
.\Tools\native-milestone.ps1 -Action Seed
.\Tools\native-milestone.ps1 -Action Read
.\Tools\native-milestone.ps1 -Action Read -After 2
```

The build action refuses to run while observed xlang3 benchmark processes are live. Exit 3 means deferred, not passed. Build/configuration and contract failures propagate nonzero exit codes. The launcher never executes CPython. Build output and the demo database are separate from the installed WorkSense application and other xlang3 checkout.

Delivered evidence: Release build, passing default embedded adapter/repository contracts, seed/read in separate native processes and exact cursor replay. Affected-row assertions remain intact. Wider Python engine/A2A historical failures and graph closure behavior are separate outstanding investigations; the native milestone does not establish their resolution.

## Following milestones

Verified native creation/SDK checkpoint: [isolated run 37621404384](https://github.com/xlang-foundation/xMind/actions/runs/37621404384), exact source `e93f28c7e6d7c214f1c0d5e049ad9408128b4ad5`, passed Release compilation and all **31 native and 30 extension contracts**. Actual files establish approved creation including empty/Unicode content, no overwrite, recorded parent identity, directory-junction rejection, denial/cancellation before dispatch, real result continuation, pending-approval restart and actual creation followed by an outcome-storage fault recovered as uncertain without replay. Official modern/legacy MCP SDK peers and both native schema dialects also passed. [Complete CTest output](evidence/native-file-creation-ci-ctest.log), [provenance](evidence/native-file-creation-ci-provenance.json), [scope and remaining creation work](native-file-creation.md). Inference is synthetic; populated editor/live-provider execution, cancellation after physical creation, process death during partial writes and attributed creation reconciliation remain unverified. The actual unseeded sidebar now runs this tested development bundle with its preserved session/profile and no configured model: [screenshot](evidence/vscode-native-creation-bundle-sidebar.png), [launch metadata](evidence/vscode-native-creation-bundle-launch.json). The next coding component is the [native process executor](native-process-tools.md); its acceptance contract is documented and implementation remains pending. Earlier pending/source-only statements below are superseded by this exact result. The full project goal remains active.

Native creation implementation prepared, **not yet natively verified**: [source scope](native-file-creation.md). The agent/executor and opened-handle create-new path now have contracts for actual files, conflicts, parent identity, junction rejection, Unicode/empty content and outcome-storage fault/restart; model/HTTP/CLI creation cases are prepared too. All **30 extension contracts passed locally** for absence-aware review/comparison and uncertainty rendering: [original extension output](evidence/vscode-native-creation-review.log). Local native compilation deferred during the separate xlang3 full timing suite. The prior thirty-contract SDK [CI run](https://github.com/xlang-foundation/xMind/actions/runs/37619215608) failed before native configuration because two `npm.cmd` paths were combined; [actual error excerpt](evidence/native-mcp-sdk-ci-resolver-failure.log). Corrected source `e93f28c7e6d7c214f1c0d5e049ad9408128b4ad5` selects one application and schedules the complete proposed 31-contract [isolated run 37621404384](https://github.com/xlang-foundation/xMind/actions/runs/37621404384). New native behavior is pending, and the goal remains active.

Official SDK tool interoperability checkpoint: source `bbeb4db077adcb5cbb86c9e91d3d3fe199886e0f` compiled in Release and passed all **30 native contracts locally**, including actual modern SDK 2.3.1 and legacy SDK 1.32.1 stdio peers. The native agent/server/admin/CLI path verifies discovery/protocol selection, encrypted environment, exact incoming argument bytes, approved real file effects, denial/cancellation, result continuation and persisted lost-reply uncertainty without replay. Native schema validation now retains distinct Draft-07 and 2020-12 semantics inside the existing bounded worker; tests exercise legacy tuple/dependency rules and `$ref` sibling differences. [Original full build/CTest output](evidence/native-mcp-sdk-local-build-ctest.log), [source/runtime/worker and pinned SDK provenance](evidence/native-mcp-sdk-local-provenance.json), [fixture scope](../Native/tests/sdk/README.md). Inference is synthetic. Full SDK feature coverage, HTTP/OAuth/resources/prompts, modern interactive continuations, account-resource mapping, complete schema conformance and live/populated UI execution remain incomplete. [Isolated CI run 37619215608](https://github.com/xlang-foundation/xMind/actions/runs/37619215608) is in progress; the goal remains active.

The preceding configured-MCP revision `1a3d5c1d5542cc1ce8eb3db1162063f7e5425b9c` passed isolated [CI run 37617251064](https://github.com/xlang-foundation/xMind/actions/runs/37617251064), all **28 native and 28 extension contracts**: [CTest output](evidence/native-agent-mcp-ci-ctest.log), [provenance](evidence/native-agent-mcp-ci-provenance.json). It predates Draft-07 validation and official SDK peer contracts.

Checkout/preview checkpoint: source, Git history, editor profile and session database now live in `D:\CantorAI2026\xMind`. The actual normal development host reopened its existing session with Explorer left and xMind/composer/model chooser right, using the locally verified MCP integration binary and no model or seeded messages: [launch evidence](evidence/vscode-xmind-checkout-launch.json), [actual screenshot](evidence/vscode-xmind-checkout-sidebar.png). A fresh CMake configure/Release build in `xMind\build\native` passed all **28 native contracts** after relocation: [original complete output](evidence/native-xmind-root-build-ctest.log), [source/binary provenance](evidence/native-xmind-root-build-provenance.json). Native and extension implementation trees are unchanged from `1a3d5c1`; intervening commits update documentation/evidence. Only prior locked build artifacts remain under the old `AgentFlow\build` directory; the active checkout and fresh build use xMind.

Native configured MCP agent checkpoint: Release compilation and all **twenty-eight** native contracts passed locally at `1a3d5c1d5542cc1ce8eb3db1162063f7e5425b9c`. The real compiled administrator persists registered stdio configuration and command-bound DPAPI credential references through embedded xlang3. The actual server restores those settings, discovers subprocess tools per run, exposes them to the agent and routes exact proposals through controller approval and durable multi-resource claims. Independent MCP peers actually write fixture files; synthetic inference verifies continuation after peer acknowledgement, denial/cancellation and lost-reply uncertainty retained across restart without replay. All **twenty-eight extension contracts** passed, including external-tool approval labels and uncertainty rendering. [Original build/CTest evidence](evidence/native-agent-mcp-local-build-ctest.log), [source/binary provenance](evidence/native-agent-mcp-local-provenance.json), [renderer/extension evidence](evidence/vscode-mcp-renderer.log), [trusted setup](native-mcp.md). Actual populated editor MCP execution, live inference, SDK interoperability, Streamable HTTP/OAuth, account mapping and complete schema conformance remain incomplete. [Isolated CI run 37617251064](https://github.com/xlang-foundation/xMind/actions/runs/37617251064) is in progress; the goal remains active.

Native MCP resource/worker checkpoint: Release compilation and all **twenty-six** native contracts passed locally at `9582dee1e17f37fe5e00220dda4c1a63f53188fe`. Repository schema four atomically locks the opened workspace and stable configured-server resources; real subprocess effects verify cross-workspace quarantine, while repository fixtures verify contention, revision changes and migration/backfill/rollback. Schema admission and input/output validation use a bounded native child with real Windows memory/time/process budgets. Independent fixtures verify actual memory-allocation refusal, stalled-child timeout/cancellation and subsequent healthy production-worker validation. Worker failures, genuine schema rejection and peer tool errors remain distinct; claimed cancellation/result-storage failures retain fail-stop ownership. [Complete build/CTest evidence](evidence/native-mcp-resources-schema-local-build-ctest.log), [source/binary provenance](evidence/native-mcp-resources-schema-local-provenance.json). Its isolated [CI run 37614949089](https://github.com/xlang-foundation/xMind/actions/runs/37614949089) also passed, all twenty-six native and twenty-seven extension contracts: [CTest evidence](evidence/native-mcp-resources-schema-ci-ctest.log), [provenance](evidence/native-mcp-resources-schema-ci-provenance.json). That revision predates registered agent activation above. SDK interoperability, account-resource mapping and full upstream schema conformance remain incomplete; exact OS CPU-timer scheduling is not proved.

The preceding approval-backed MCP source passed [isolated CI run 37610809269](https://github.com/xlang-foundation/xMind/actions/runs/37610809269) at `5b24f26a6ee2dd5e3da06aaee6169b5513a6a980`, all twenty-five native and twenty-seven extension contracts: [CTest evidence](evidence/native-mcp-effects-ci-ctest.log), [provenance](evidence/native-mcp-effects-ci-provenance.json). That revision predates shared-resource migration and schema-worker containment; it does not prove their isolated CI result.

Native approval-backed MCP checkpoint: Release compilation and all **twenty-five** native contracts passed locally at `5b24f26a6ee2dd5e3da06aaee6169b5513a6a980`. Actual independent MCP subprocesses wrote real fixture files only after exact durable approval. Tests cover denied/cancelled/pre-dispatch effects, duplicate identities, exact numeric/schema/config binding, invalid catalogs/arguments/output, lost/error replies after actual effects, workspace quarantine and outcome-storage fault/restart without replay. Native JSON Schema 2020-12 validation and all 176 pinned library/source license hashes passed too. [Complete build/CTest evidence](evidence/native-mcp-effects-local-build-ctest.log), [binary/source provenance](evidence/native-mcp-effects-local-provenance.json). Peer acknowledgement is explicitly distinguished from independent verification. Registered configuration persistence, agent/CLI/view wiring, SDK interoperability, full schema conformance, adversarial evaluation containment and server/account-wide resource locking remain incomplete. [Isolated CI run 37610809269](https://github.com/xlang-foundation/xMind/actions/runs/37610809269) is in progress; this is a local component checkpoint, not full MCP/product readiness.

The preceding native catalog client additionally passed [isolated CI run 37608803467](https://github.com/xlang-foundation/xMind/actions/runs/37608803467) at `fe07a95d5984748aaabbd744a76bda9d75bb45ef`, all twenty-three native and twenty-seven extension contracts: [CTest evidence](evidence/native-mcp-client-ci-ctest.log), [provenance](evidence/native-mcp-client-ci-provenance.json). That revision precedes schema/effect integration and does not prove the new twenty-five-contract source.

Isolated negotiation checkpoint: [CI run 37607766121](https://github.com/xlang-foundation/xMind/actions/runs/37607766121) passed all twenty-two native and twenty-seven extension contracts at `809ef148ba5a16778a0a1a98ca06637394497c00`. Actual independent peers verify modern discovery, legacy error/timeout fallback, retired late discovery replies, initialized notification ordering and each supported legacy revision (`2025-11-25`, `2025-06-18`, `2024-11-05`). [CTest evidence](evidence/native-mcp-handshake-ci-ctest.log), [provenance](evidence/native-mcp-handshake-ci-provenance.json). That revision precedes the peer-request reply helper and catalog client; it does not establish their isolated CI result or model/tool integration.

Native MCP client checkpoint: Release compilation and all twenty-three native contracts passed locally at `fe07a95d5984748aaabbd744a76bda9d75bb45ef`. The owned native subprocess client discovers real peer catalogs with pagination, validates bounded descriptions, handles legacy ping/unsupported methods and retires on malformed or interrupted exchanges without reconnect/replay. Modern/legacy negotiation and transport contracts also passed. [CTest evidence](evidence/native-mcp-client-local-ctest.log), [runtime binary provenance](evidence/native-mcp-client-local-provenance.json). Isolated CI is pending. Tool execution remains absent until policy/journaling integration; full schema validation, resources/prompts and SDK interoperability remain required.

Native MCP request-tracking checkpoint: [CI run 37602231619](https://github.com/xlang-foundation/xMind/actions/runs/37602231619) passed all twenty native and twenty-seven extension contracts at `276164689deacb130a66fce4362ad94d07cd12b7`. Actual compiled correlation fixtures verify bounded admission, out-of-order matching, peer errors, duplicates/unknown IDs, cancellation, initialization cancellation rejection, abandonment and retirement bounds. [CTest log](evidence/native-mcp-requests-ci-ctest.log), [provenance](evidence/native-mcp-requests-ci-provenance.json). Windows stdio/process fixtures subsequently passed [CI run 37603832587](https://github.com/xlang-foundation/xMind/actions/runs/37603832587), all twenty-one native and twenty-seven extension contracts at `a98cf0bc59d0088d9e64a36b43d064b5bcbaf3cc`, including waiting-reader cancellation. [CTest evidence](evidence/native-mcp-stdio-cancellation-ci-ctest.log), [provenance](evidence/native-mcp-stdio-cancellation-ci-provenance.json). Modern/legacy negotiation and peer-request replies subsequently passed locally with the twenty-three-contract client checkpoint above; isolated CI and tool/policy integration remain required.

Native MCP wire checkpoint: [CI run 37601682734](https://github.com/xlang-foundation/xMind/actions/runs/37601682734) passed Release compilation, all twenty native contracts and twenty-seven extension contracts at `f0e44b029d6a28a57a80f77e921641301105a104`. The new real C++ codec passed bounded fragmented JSON-RPC/UTF-8 fixtures, precise IDs, modern request metadata and rejection/EOF behavior. [CTest log](evidence/native-mcp-wire-ci-ctest.log), [provenance](evidence/native-mcp-wire-ci-provenance.json). Added request tracking and Windows owned-process/stdin/stdout transport are subsequent source awaiting their own native runs. This is not full MCP peer/tool integration, A2A or graph completion.

Verified uncertainty inspection checkpoint: [CI run 37600409106](https://github.com/xlang-foundation/xMind/actions/runs/37600409106), revision `cbc5f26c3c1842a54013f94cbf3baf303761b568`, passed all nineteen native and twenty-three extension contracts. The actual production server reopened the fixture journal without a model, inspected bounded raw file bytes through HTTP/CLI/editor host client, rejected another workspace and preserved the uncertain operation/events. [CTest evidence](evidence/native-uncertain-inspection-ci-ctest.log), [provenance](evidence/native-uncertain-inspection-ci-provenance.json). Its packaged runtime now runs in the [actual unseeded sidebar](evidence/vscode-inspection-bundle-launch.json); no inspection workspace or provider is configured in this preview. Attributed resolution and actual populated recovery UI remain incomplete. The new MCP codec is separate source awaiting the twenty-contract native gate.

Recorded-run history checkpoint: the sidebar now exposes each backend run in the selected conversation, preserving full transcript history while choosing older events/operations. Selected-run identity persists by origin/session, and IDs from another conversation are rejected before fetching their details. Older uncertain edits remain inspectable. Selecting historical work preserves observation of another active run and blocks additional submissions. [Twenty-seven extension contracts](evidence/vscode-run-history.log) passed against labeled HTTP/VS Code API/DOM fixtures. The [actual unseeded preview reopened](evidence/vscode-run-history-preview.json) with saved SecretStorage against the existing verified native bundle; [screenshot](evidence/vscode-run-history-preview.png). Actual populated history rendering and live model execution remain separate verification requirements.

Uncertain-edit inspection source checkpoint: native read-only HTTP/CLI access and an explicit model-free inspection workspace now connect the existing raw-file inspector to clients. The VS Code sidebar offers **Inspect actual file**, displays the backend observation and preserves quarantine without any grant/replay/restore action. [Twenty-three extension contracts](evidence/vscode-uncertain-inspection.log) passed locally, including escaped observations and selection invalidation. Native production restart/identity/journal contracts are added and await hosted CI; this entry does not claim their pass or actual uncertain-edit rendering in the interactive preview.

Verified development bundle checkpoint: [CI run 37598517059](https://github.com/xlang-foundation/xMind/actions/runs/37598517059) passed all nineteen native contracts, including selected-model CLI initiation of an actual approved file edit, and twenty extension contracts. Its packaged C++ server and embedded xlang3 runtime now run in the actual local VS Code preview with the existing session database, right sidebar and bottom model selector. [CTest evidence](evidence/native-model-edit-cli-ci-ctest.log), [launch evidence](evidence/vscode-ci-bundle-launch.json), [actual screenshot](evidence/vscode-ci-bundle-sidebar.png). Inference contract peers are synthetic; the preview remains unconfigured for live inference. The development bundle requires external pure standard-library source and is not a complete installer.

Native model/edit integration checkpoint: Release compilation and all **nineteen native contracts** passed on an isolated Windows runner, with stock pinned xlang3 plus the reviewed SQLite prerequisite and allowed standard-library source. Actual model-invoked file application, denial/stale/cancel handling, model selection, supplied usage/measured timings, restart retention and uncertain-file inspection passed using labeled synthetic inference peers. [CI run](https://github.com/xlang-foundation/xMind/actions/runs/37595553681), [complete CTest evidence](evidence/native-model-edit-ci-ctest.log), [provenance](evidence/native-model-edit-ci-provenance.json). All twenty extension contracts also passed. Live-provider/coding completion, new-file/process tools, attributed reconciliation, MCP/A2A, graphs and team/Electron scope remain incomplete. The local preview has no model; its subsequent bundle upgrade is recorded above.

Persistent interactive preview checkpoint: the launcher now uses a normal VS Code extension development host. The old `--extensionTestsPath` bootstrap selected in-memory VS Code storage, losing its server access token on restart; that test mode is retained only for actual tests. Normal-host development bootstrap clears private environment variables, completes activation before opening the view, and records a unique readiness marker. Actual close/reopen reused the saved SecretStorage token against the same native backend without supplying another token. [Reopen evidence](evidence/vscode-preview-reopen.json), [actual screenshot](evidence/vscode-persistent-sidebar.png), and [twenty passing extension contracts](evidence/vscode-preview-persistence.log). No model is configured. Actual read-only diff integration also passed again after activation changes.

Actual VS Code diff checkpoint: `Tools/test-vscode-review.ps1` launches an isolated official VS Code host and runs the product snapshot/diff adapter against labeled test bytes. VS Code 1.140.0 opened a real native diff tab with exact before/after documents; typing and filesystem writes could not alter the read-only snapshot, which stayed clean. [Actual-host evidence](evidence/vscode-native-diff.json). This is editor API integration evidence, not a rendered screenshot or live model/approval/file-effect test. The interactive preview is not seeded with these fixtures.

VS Code reconnect checkpoint: Refresh now rechecks backend health/model capabilities, reloads the selected conversation and resumes observation without creating or replaying an agent run. Configured model selection persists by backend origin across view reopening; removed IDs fall back to the backend default. The composer draft stays in the view. Nineteen deterministic extension contracts pass: [reconnect evidence](evidence/vscode-reconnect.log). This verifies adapter behavior with HTTP/host fixtures; actual backend model reconfiguration/live provider execution remains unverified.

VS Code edit comparison checkpoint: pending file approvals now offer **Compare changes**, opening the recorded before/after snapshots in VS Code's native read-only diff editor. The host fetches and revalidates the reviewed operation before opening; webview-supplied text is ignored, and comparison sends no approval or filesystem write. Snapshot storage is bounded to 32 comparisons/32 MiB per extension activation. Seventeen client/host/renderer fixture contracts pass, including exact snapshot comparison and changed-proposal rejection: [comparison evidence](evidence/vscode-edit-comparison.log). Actual live model-driven proposal/diff interaction remains unverified while the native integration is awaiting compilation.

Right-sidebar chat checkpoint: the real VS Code 1.140 development host renders xMind in the right secondary sidebar, with Explorer left and the composer/model selector anchored below scrolling history. Fifteen client/host/DOM tests cover sanitized Markdown/code, per-response usage badges, missing metrics, approvals, backend-advertised model choices and live-prefix preservation. [Actual unseeded UI screenshot](evidence/vscode-right-sidebar.png) and [test evidence](evidence/vscode-sidebar-renderer.log) verify that scope. No model is configured in the preview. Native per-run model selection, persisted timings/usage and model-invoked edits are pending verification behind the live benchmark build guard; live history/inference and full coding completion remain unverified.

VS Code approval view checkpoint: the view displays recorded operation payloads and before/after text, and sends allow/deny choices through the authenticated host adapter. The host requires a reviewed pending proposal in the selected run and revalidates the exact backend record before granting. Eight deterministic client/host contracts pass, including changed/unreviewed/repeated decision rejection and disposal during revalidation: [approval host evidence](evidence/vscode-approval-host.log). Actual IDE rendering, model-invoked edits, reconciliation and team permissions remain unverified or pending.

Native local approval API checkpoint: authenticated local-owner inspection/decision routes, CLI `operations`/`operation`/`decide`, and the thin VS Code host client now connect to durable exact proposals. The actual compiled server and executor apply approved real-file edits and reject stale/denied effects. [Approval API evidence](evidence/native-approval-api-ctest.log) covers eighteen contracts, including unauthorized/spoofed decisions and absent client effect-mutation routes. Agent write-tool integration, approval UI, reconciliation and team scopes remain pending.

Native approved edit component: `EditExecutor` now connects exact durable before/after proposals, attributed approval and one-use claims to actual file application and outcome journaling. Real file/xlang3 tests cover approval, denial, cancellation, stale plans, duplicates and reopen. A fixture-only outcome-storage fault after the actual edit verifies that the file effect remains observable and restart quarantines the unresolved claim. [Approved edit evidence](evidence/native-approved-edit-ctest.log) covers seventeen native contracts. Public controller authorization/routes, agent write tools, partial-write/process-kill testing and reconciliation remain pending.

Native file application component: the backend-only Windows primitive revalidates the exact file snapshot, applies real in-place changes and checks the resulting bytes. Real disk contracts cover longer/shorter/empty edits, stale content, identity/hash/boundary rejection, pre-write cancellation and competing handles. [Application evidence](evidence/native-file-application-ctest.log) covers the sixteen native contracts. Integrated approval/execution, post-write fault recovery and reconciliation remain pending; model tool definitions still offer only read operations.

Native authorization/planning checkpoint: schema-v3 operation records and `PermissionWaiter` now preserve exact approvals, controller attribution, single-use claims, cancellation/expiry, workspace exclusion and uncertain recovery. Workspace identity, same-handle content snapshots and literal edit planning passed against real filesystem fixtures and independent Node hashes. All sixteen native contracts passed: [native-permission-planning-ctest.log](evidence/native-permission-planning-ctest.log). These are library components; public approval controllers, integrated approved file execution and reconciliation remain required before exposing a write tool.

Native execution/server checkpoint: [AgentRunner and AgentService](native-agent-loop.md) invoke the native provider and actual read tools, with encrypted credential resolution, atomic conversation/state persistence, bounded native workers, HTTP/CLI scheduling, cancellation and run deadlines. All fifteen native contracts passed, including actual editor host client requests to the compiled server. Evidence: [native-execution-server-ctest.log](evidence/native-execution-server-ctest.log). Live inference, coding-task completion and actual editor UI validation remain pending.

Native workspace tools checkpoint: [actual C++ filesystem tools](native-workspace-tools.md) read, list and search authorized Windows workspace files, with handle checks, argument validation and bounded results. Real filesystem fixtures and native agent integration passed. Mutation/process tools, permission policy and complete coding workflows remain required.

Native HTTP/console checkpoint: [xMind Server](native-server.md) exposes session persistence and configured native execution, with authenticated independent HTTP/CLI/editor-host clients. Runs and cancellation are owned by the real engine; manual lifecycle transitions and fabricated client assistant messages are rejected. Read-tool execution, cancellation and durable event observation passed with synthetic inference peers. Live provider and full coding workflows remain required.

Native persistence worker checkpoint: [PersistenceService](persistence-service.md) owns embedded xlang3 and the repository on one thread, serving typed requests from concurrent backend callers. All five native contracts passed, including concurrent writes, queue backpressure, owner recovery, failure cleanup and shutdown draining. It is ready for native HTTP/agent callers; those services and their authentication remain incomplete.

Credential storage checkpoint: the C++ repository now protects and persists credentials through xlang3, with scope/purpose binding, revision-checked rotation/deletion and retired identities. Atomic schema-v1 migration preserves existing messages. All four native contracts passed; the original milestone database also reopened and replayed events after migration. Committed evidence: [credential-repository-ctest.log](evidence/credential-repository-ctest.log). No team authorization or public credential endpoint is claimed.

- Native xMind Server + CLI: shared sessions/run events, lifecycle, authentication and reconnection.
- Coding engine: real model/tool task with explicit approvals, verification and reviewable changes.
- Protocols and graphs: independent MCP/A2A peers, graph branching/pauses/checkpoint recovery.
- Clients: browser UI and VS Code workflows over the local backend, with actual UI validation.
- OSS release: reproducible native builds, scoped parity evidence, documented limitations and end-to-end local coding/protocol/provider acceptance.
- Connection profiles: Local single-user mode plus optional Nexus binding over the shared protocol, scoped identities/state, authenticated local agent/workspace enrollment and real reconnect/lease/recovery acceptance. Nexus owns the private team-server/distributed implementation.

These milestones retain the revised xMind OSS scope. Team-server features, PostgreSQL, WebRTC and Electron belong to Nexus and are excluded. Relevant OpenCode coding parity and agreed broad model/provider support remain required.
