# Working xMind v1 delivery milestone

User priority, 2026-10-25: working v1 is the only delivery goal. Deliver a working first version with a solid UI and reliable framework. The user removed the old broader goal; do not resume or pursue its feature backlog. Existing implementation, user data and historical Git/evidence records remain preserved.

## Immediate goal

Deliver an installable, verified xMind v1 whose browser UI, VS Code extension and CLI support a reliable end-to-end coding workflow through the shared native C++ backend, embedded xlang3 and SQLite. Workers use standard local xlang3 IPC root objects and subobjects. Prioritize release blockers and usability over feature expansion.

## V1 acceptance

- The packaged version starts reliably and exposes usable browser, VS Code and CLI entry points.
- Workspace creation/selection/switching changes the actual backend context, files and scoped history consistently.
- The UI has a stable conversation/footer layout, responsive controls, visible progress, clear errors and explicit unavailable states.
- Provider setup and model selection support the working coding path without leaking credentials. Development verification uses controlled peers rather than paid inference.
- A coding task can read files, propose and apply approved edits, request command approval and return actual command results. Denial and cancellation behave predictably.
- History, settings, workspace selection and completed task state restore correctly after reload/restart; restoration never replays completed effects.
- The actual packaged browser, extension and CLI each receive fresh end-to-end verification. Existing successful task histories, owners, user settings, profiles and backups remain preserved.
- Installation uses qualified artifacts with a verified backup, a preservation audit and a fresh installed smoke check. Local unit/contract passes alone do not establish release readiness.

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
