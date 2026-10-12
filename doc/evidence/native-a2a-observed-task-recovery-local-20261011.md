# A2A stream-observed task recovery — 2026-10-11

The native outbound A2A tool now preserves a task and context ID observed in a
stream before a later stream/event-storage failure. The original operation
remains `uncertain`, so xMind never retries the send or claims a terminal remote
outcome. Owner-authenticated task inspection can use that saved task ID to issue
read-only `GetTask` against the same revision-pinned registered peer.

The native contract injects a failure from the first streamed-task event
observer after the peer has delivered its task ID. It verifies both IDs remain
in the uncertain operation, closes and reopens the xlang3/SQLite persistence,
then inspects the task through the authenticated HTTP API. The peer sees no
second send. A separate uncertain dispatch with no task ID still returns HTTP
409 without contacting the peer. Browser and VS Code operation controls permit
inspection in this state, but do not offer cancel or follow-up actions.

Validation on the current worktree:

- Release build of `agentflow_a2a_agent_registry_contract`: passed, 0 warnings,
  0 errors.
- Native A2A client, official SDK peer and registry/effect contracts: **3/3**
  passed (`ctest --test-dir build/native -C Release -R
  '^native_a2a_(agent_registry|client|client_sdk)_contract$`).
- VS Code suite: **314/314** passed.
- Browser suite: **96/96** passed.

The remote peer is a local synthetic protocol fixture and the event-journal
failure is injected at the callback boundary. This verifies durable recovery
and inspection behavior, not a live external A2A service, stream resubscription,
or rendered/installed live-model interoperability. A later local slice adds
explicit `includeArtifacts` task inspection; peer correctness and artifact UI
rendering still require the independent-peer gate.
