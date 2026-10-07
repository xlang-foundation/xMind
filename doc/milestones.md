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

Native persistence worker checkpoint: [PersistenceService](persistence-service.md) owns embedded xlang3 and the repository on one thread, serving typed requests from concurrent backend callers. All five native contracts passed, including concurrent writes, queue backpressure, owner recovery, failure cleanup and shutdown draining. It is ready for native HTTP/agent callers; those services and their authentication remain incomplete.

Credential storage checkpoint: the C++ repository now protects and persists credentials through xlang3, with scope/purpose binding, revision-checked rotation/deletion and retired identities. Atomic schema-v1 migration preserves existing messages. All four native contracts passed; the original milestone database also reopened and replayed events after migration. Committed evidence: [credential-repository-ctest.log](evidence/credential-repository-ctest.log). No team authorization or public credential endpoint is claimed.

- Native xMind Server + CLI: shared sessions/run events, lifecycle, authentication and reconnection.
- Coding engine: real model/tool task with explicit approvals, verification and reviewable changes.
- Protocols and graphs: independent MCP/A2A peers, graph branching/pauses/checkpoint recovery.
- Clients: VS Code and Electron workflows over the shared backend, with actual UI validation.
- Team deployment: authenticated users/workers, project isolation and shared-session behavior.

These milestones retain the full product scope; none substitutes for full OpenCode parity or all agreed model/provider support.
