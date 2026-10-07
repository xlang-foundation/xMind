# Reviewable milestones

The user requested visible milestones. Show a runnable result, validation evidence and remaining scope at each milestone. Prepared source or a diagram alone does not prove a runnable native milestone.

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
- Clients: VS Code and Electron workflows over the shared backend, with actual UI validation.
- Team deployment: authenticated users/workers, project isolation and shared-session behavior.

These milestones retain the full product scope; none substitutes for full OpenCode parity or all agreed model/provider support.
