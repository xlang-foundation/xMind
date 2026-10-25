# Working xMind v1 delivery milestone

User priority, 2026-10-25: deliver a working first version with a solid UI and reliable framework. All planned features are not required for this milestone. Preserve the original broader delivery goal below for subsequent work; do not mark that broader goal complete merely because v1 is ready.

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

## Deferred breadth

Advanced graph workflows, wider provider coverage, additional MCP/A2A workflows and generic connection enhancements are later work unless needed to unblock the v1 path. Preserve functioning existing capabilities. Do not present unfinished controls as working. Cross-node worker execution belongs to later Cantor Cluster work; private Nexus features and broad xlang3 optimization remain excluded.

## Current evidence and remaining release work

Checkpoint1f49b5c3 records complete local184/184 native qualification against SDK6df6520f, plus the SDK pip25/26 and fixed performance evidence. This does not qualify a new packaged or installed release. Installed0.1.76 is preserved. Hosted qualification, new package assembly and actual packaged/rendered/installed v1 acceptance remain open. See [validation status](VALIDATION_STATUS.md) and [local native receipt](evidence/native-sdk6df-full-20261025.json).

## Original broader goal — retained verbatim

Continue the recovered xMind delivery goal in D:/CantorAI2026/xMind: deliver and verify usable coding in the browser UI, VS Code extension and CLI through the shared native C++ backend, embedded xlang3 and SQLite; support single/graph agents, providers, MCP/A2A and OpenCode-style coding capabilities within documented OSS scope. Resume installed 0.1.52 checkpoint and unfinished conversation/footer layout work, then remaining product and rendered coding verification. Follow AGENTS.md, preserve existing owners/history/settings, never replay completed model/tool tasks, use no CPython, keep visible windows closed unless requested, and commit/push verified reviewable checkpoints. OpenCode/LiteLLM are references; Nexus private features and broad xlang3 optimization are excluded.
