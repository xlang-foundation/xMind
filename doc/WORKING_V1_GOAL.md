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

## Current evidence and remaining release work

Checkpoint1f49b5c3 passes local184/184 and hosted182/182 against SDK6df6520f, including hosted packaged offline pip25.2; SDK pip25/26 and fixed performance evidence remain separate. Candidate0.1.77 VSIX ead875de passes fresh packaged CLI, hidden browser and Code coding; Code reload requires reauthentication. Candidate managed startup/cancellation/restart is reconciled without task replay. Workspace creation/switching/reload works, but full browser-view restart loses the selected workspace: this blocks v1. Provider/remaining usability checks, the selection repair and safe installation remain open. Installed0.1.76 is preserved; no installed/release acceptance is claimed. See [validation status](VALIDATION_STATUS.md) and [workspace restart evidence](evidence/packaged-sdk6df-workspace-restart-20261025.json).

## Test artifact retention

Keep compact summaries and essential failure diagnostics. Reuse the current build and avoid duplicate runtime/package copies or successful fixture histories. Preserve real user history/settings, qualified release inputs and required rollback/performance baselines. A cleanup inventory identified3.17 GiB of obsolete compiler intermediates; automatic policy review blocked their removal, so they remain on disk. The private plan records the exact targets and executed=false.
