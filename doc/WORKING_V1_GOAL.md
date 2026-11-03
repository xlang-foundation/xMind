# Working xMind v1 delivery milestone

User priority, 2026-10-25: working v1 is the only delivery goal. Deliver a working first version with a solid UI and reliable framework. The user removed the old broader goal; do not resume or pursue its feature backlog. Existing implementation, user data and historical Git/evidence records remain preserved.

## Immediate goal

Deliver an installable, verified xMind v1 whose browser UI, VS Code extension and CLI support a reliable end-to-end coding workflow through the shared native C++ backend, embedded xlang3 and SQLite. Workers use standard local xlang3 IPC root objects and subobjects. Prioritize release blockers and usability over feature expansion.

## V1 acceptance

- The packaged version starts reliably and exposes usable browser, VS Code and CLI entry points.
- Workspace creation/selection/switching changes the actual backend context, files and scoped history consistently.
- The UI has a stable conversation/footer layout, responsive controls, visible progress, clear errors and explicit unavailable states.
- Provider setup and model selection support the working coding path without leaking credentials. Use controlled peers for contract tests and the configured CantorServe endpoint for authorized project exercises.
- A coding task can read files, propose and apply approved edits, request command approval and return actual command results. Denial and cancellation behave predictably.
- History, settings, workspace selection and completed task state restore correctly after reload/restart; restoration never replays completed effects.
- The actual packaged browser, extension and CLI each receive fresh end-to-end verification. Existing successful task histories, owners, user settings, profiles and backups remain preserved.
- Installation uses qualified artifacts with a verified backup, a preservation audit and a fresh installed smoke check. Local unit/contract passes alone do not establish release readiness.

## Current continuation — 2026-11-02

- 2026-11-03: Original cold profile now starts on Native9ea/SDK92aa at the unchanged profile/workspace paths. Actual model-free browser graphs pass read, paused reload/human answer and cancellation. An older completed root restores exactly after reload; CLI graph-watch only observes saved events. Supported live handoff45424→14932 preserves API62774/browser52887 and exact5messages/3roots/28events, with automatic auth and no replay. All original project fixtures remain unchanged. Six obsolete runtime copies (684412569bytes) could not be removed: automatic review rejected cleanup as blocked by policy; no alternate route. Complete CantorServe coding, stock-editor refresh and packaged acceptance remain pending. [Evidence](evidence/cold-client-graphs-20261103.json), [UI](evidence/cold-client-graphs-20261103.png).

Latest November3 checkpoint: Native9ea02226 with SDK92aaafb5 passes the complete189 checks in891.03s with23911 unchanged inputs, environment and tools. SDK60 correctness checks and all11 fixed performance cases pass. The same staging/profile/workspace paths recovered through supported stopped-owner upgrade after exact idle-owner retirement; immediate pre/post snapshots preserve59messages,2sessions,5roots and12settled operations. A fresh ordinary version0.1.1 turn applies one approved edit, but its second model dispatch fails with465288ms root budget remaining; no tests run. Separate source-client failure explanations pass all221 browser and538 editor checks; actual auto-auth/history/run restoration is verified. Stock Code Server nested refresh remains blocked independently of Native writes: its mandatory Windows absolute exclusion reproduces the parcel regex error, while the equivalent normalized control subscribes. Vendor code is unchanged. Two disposable controls were removed; original project fixtures, installed80 and backups remain preserved. Complete coding, remaining client/feature and packaged installation acceptance remain pending. [Current evidence](evidence/source-client-and-serving-blockers-20261103.json), [runtime/SDK detail](evidence/bytecode-inheritance-and-unicode-20261103.json).

The goal above remains the delivery target. The qualified 0.1.79 release and
0.1.80 authentication fix are historical baselines; they do not qualify all
subsequent main changes as a new installed release. This continuation started
from main `952a8a178613f0bb1e9560896cc8114cb9a7af5e`, with sibling xlang3 main
`6186ffa61203789ed8ff18669c81fff32125259c`.

- While CantorServe deployment is pending, reproduce and fix local product
  blockers without model inference. Preserve C++ ownership, embedded xlang3,
  SQLite and xlang3 IPC; execute Python tooling only with xlang3.
- The recorded VS Code earlier-run reset is reproduced and repaired in source.
  Complete extension tests pass; installed editor verification remains pending.
  [Reproduction and scope](evidence/vscode-run-restoration-20261102.json).
- CantorServe has one recorded successful xMind public coding exercise with
  eight unchanged tests. Ordinary multi-turn coding needs fresh qualification
  against the deployed endpoint. Earlier failed runs remain preserved;
  benchmark completion and general coding reliability are not established.
  [Latest model evidence](evidence/demo-20261029.json).
- Reassess the recorded native process timeout and xlang3 compatibility findings
  against current sources before treating old failures as current defects.
  Project-generated errors are evidence of coding quality, not authorization
  to manually correct the projects or coach their diagnostic recovery.
- After fixes, qualify the resulting packaged browser, VS Code and CLI workflows,
  including restart/restoration, approvals and cancellation, before replacing
  the installed release. Preserve user data, settled runs and required backups.

User continuation, 2026-11-02: use the deployed CantorServe endpoint to develop
xMind against real projects and test all existing features. This includes coding,
file and command approvals, denial/cancellation, workspace/history restoration,
all three clients, MCP, skills, and existing graph workflows. Test existing
capabilities without expanding the product backlog. Separate complete registered
contract inventories from actual model-backed UI/project acceptance, and mark
unsupported or untested cases explicitly. Preserve failed runs and original
project tests; do not coach diagnostic recovery through project prompts.

CantorServe responds at `http://127.0.0.1:19445/v1`; encrypted project-key discovery,
model save and page restoration pass after the provider-route fix. After the
reported serving repair, one new ordinary feature turn received seven complete
model responses but reached its overall deadline after eight missing-parent file
calls. Both failed roots are preserved and must never be replayed; all three
original project files remain unchanged. No coding acceptance is claimed.

The earlier SDK long-path manifest existed on main but was not embedded in the
actual C++ host. The SDK build correction passes58 permitted contracts and11
fixed performance cases without changing its runtime DLL. The equivalent xMind
host manifest and an actionable, approval-preserving parent-creation error are
committed. Corrected complete Native189 passes in860.21s with23907 inputs, environment
and tools unchanged; original183/189 and corrected7/7 records are retained. Browser221/extension536
remain the qualified client source baseline. Preserve existing runtime/profile
paths and use supported handoff after qualification.
[Current reproduction, failures and scope](evidence/native-host-path-and-creation-20261103.json).

Protected handoff then exposed a first-replacement process-profile reimport bug.
Supported resume restored the same profile and origins with exact25-message
preservation; fix4aa98dfb passes its red/green regression and passes full
Native189 in856.30s with23907 inputs/environment/tools unchanged. A new process-code project turn independently used the
parent hint and created two explicitly approved files. Its xlang3 pytest command
exited1 because pytest is absent, and the run subsequently reached its deadline.
The original three files remain unchanged. No project test pass, cold-client
startup or new installed release is claimed. Never replay any of the three
failed expense roots.
[Handoff and actual project receipts](evidence/explicit-process-reimport-20261103.json).

## Outside the current delivery goal

Advanced graph workflows, wider provider coverage, additional MCP/A2A workflows and generic connection enhancements are outside this delivery goal unless needed to unblock the v1 path. Preserve functioning existing capabilities. Do not present unfinished controls as working. Cross-node worker execution belongs to later Cantor Cluster work; private Nexus features and broad xlang3 optimization remain excluded.

## Qualified working v1 delivery

Installed0.1.79 now completes the working-v1 milestone. All2380 installed files and the2336-file runtime content inventory match the qualified VSIX/manifest; Native SHA78bf31b0 and SDK6df are verified. The five preserved user-state roots have identical complete file sets/hashes, the two existing managed owners retain their exact births/images, other extension registry entries are unchanged, and exact75/76 rollback copies each verify2442 files. A fresh isolated installed read graph completes, preserves its two-message history and retires its identity-verified owner with0model calls. NEVER replay smoke session10d52bf152ced65b437c1f8f31e30cd5/root0342d66ccb3b26a510d9f2ad1231f9dd. The full before/after user-file hash inventories are retained in verified compressed storage, with identical content deduplicated. No user profile backend, migration, paid inference or visible window is used. [Installed qualification/preservation/smoke receipt](evidence/installed-0.1.79-f641f9ff-20261025.json). Code acceptance uses hidden Microsoft Code Server and requires reauthentication after reload; automatic sign-in persistence and visible desktop use remain unverified. The current Native repair passes local184/184; the earlier hosted182 result does not qualify that repair.

Candidate0.1.79 VSIX1cddb6a0 contains Native78bf31b0 at f641f9ff/SDK6df6520f. Complete local184/184 and clients510/185 pass. Actual packaged cold workspace attachment, scoped files/session, reload and full native-view/adapter restart now preserve the selected workspace; both original session rows remain identical with0runs/models. The browser preference and stale-lease defects are repaired. [Current restart/preservation evidence](evidence/packaged79-workspace-restart-20261025.json), [native qualification](evidence/native-view-lease-sdk6df-20261025.json).

Actual packaged79 provider setup reuses the configured saved key, discovers20 catalogue models, saves a model and restores the same provider/model/empty conversation after reload. There is no inference or run, and the original provider YAML stays unchanged. The isolated metadata host/adapter are retired and its private encrypted database is preserved. [Provider setup evidence](evidence/packaged79-provider-setup-20261025.json).

Fresh candidate79 actual browser acceptance now passes a denied edit without file changes, approved read/edit/Node4 coding with an automatic preview update and durable completed history, and active streaming cancellation with transport closure/worker retirement and durable cancelled state. Three separate conversations preserve their original results without replay; all local model requests use a controlled peer. [Browser acceptance evidence](evidence/packaged79-browser-approval-cancel-20261025.json).

Fresh candidate79 packaged CLI and Code coding now pass read/edit/Node4 with separate approvals and durable completed history. Code proves saved-editor selection and live buffer refresh; its reload requires reauthentication, so automatic sign-in persistence remains unverified. All test execution is controlled and local, with no paid inference. [CLI evidence](evidence/packaged79-cli-20261025.json), [Code evidence](evidence/packaged79-code-20261025.json). Qualified0.1.79 installation, preservation and installed smoke are complete as recorded above; exact75/76 rollback copies remain. Hosted182/182 and packaged offline pip25.2 qualify earlier source1f49b5c3, not the native lease repair; no new hosted job is dispatched. SDK pip25/26 and fixed performance evidence remain separate. See [validation status](VALIDATION_STATUS.md).

## Test artifact retention

Keep compact summaries and essential failure diagnostics. Reuse the current build and avoid duplicate runtime/package copies or successful fixture histories. Preserve real user history/settings, qualified release inputs and required rollback/performance baselines. A cleanup inventory identified3.17 GiB of obsolete compiler intermediates; automatic policy review blocked their removal, so they remain on disk. The private plan records the exact targets and executed=false.
