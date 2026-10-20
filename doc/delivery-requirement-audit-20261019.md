# Delivery audit in progress

Current repository checkpoint: 0a8ec093. This is not a completion declaration.

## Evidence established on installed 0.1.53

- Shared C++ backend, embedded xlang3/SQLite, installed runtime inventory: coding53-installed-20261019.json.
- Browser coding: actual IAB one-time task, reviewed edit, real Node process 4/4, reload with exact 11 messages: coding53-iab-coding-api-20261019.json and two actual browser screenshots.
- Console: exact same backend/history/operations and direct PTY human-readable restore, /runs and /exit: coding53-shared-cli-20261019.json and coding53-interactive-cli-20261019.json. No new CLI-originated model task was performed in this checkpoint.
- UI candidate: 445 extension/158 browser tests, production fixture rendering at 480/720/1000, actual IAB expanded Skills: coding53-client-gate and footer-layout receipts.
- Complete native build reproduces installed xmind executable. All 154 unique registered tests pass in 1,069.74 seconds; terminal CTest64788 exits0 at2026-10-20T01:54:28.087Z. The wrapper exits0 after verifying all source/SDK/library/dependency/binary hashes and file sets unchanged. See evidence/coding53-full-native-20261019.json and its raw log/inventory. The controlled native fixtures are separate from live provider and rendered-client evidence.

## Requirements that remain open

1. Actual installed53 VS Code rendered coding/restore and external-browser acceptance: no fresh evidence. Visible editor/browser windows are prohibited by current user constraint unless requested. IAB success does not prove these separate surfaces. Continue independent native work rather than repeat the previous desktop-control failures.
2. Agent-level process placement and authenticated worker transport: AgentDefinition has no placement field; AgentService::work directly calls runner.execute; AgentRunner still owns PersistenceService/workspace/process/MCP/A2A objects. Existing run_agent_worker executes one trusted xlang3 callable. Tool-worker passes do not prove agent-worker delivery.
3. Runtime scheduling: native candidate8b462140/db694de1 adds shared execution capacity and a cancellable FIFO for worker-ready AgentService/GraphService roots. Eight focused contracts and the complete154-contract native gate pass, including actual mixed-root order, cancellation, human pause/resume and shutdown; initial peer assertion failure is retained. Full gate1,061.80seconds verifies the complete frozen closure unchanged. Local service backlogs are still separate, so global admission-order fairness and placement remain incomplete. Candidate installation remains pending. Further work must preserve paused root reservations, one-shot wake semantics, budgets, cancellation, terminal retirement, admission exhaustion and restart without replay.
4. Reusable nested graph/super-agent runtime: GraphNodeDefinition has agent/tool/human/MCP data but no registered nested graph contract. Passing ordinary graphs does not prove bounded nesting, child ownership, cancellation propagation or immutable nested graph revisions.
5. Generic Local/Nexus protocol: connection-profiles.md explicitly reports Nexus endpoints not_connected; authentication, negotiated remote command/event routing and separate enrollment remain incomplete. Private server/team/PostgreSQL/distributed scheduler implementation is excluded. Do not count the private product as an OSS blocker or pretend saved address metadata proves remote integration.
6. Broader native providers, MCP/A2A and authoring: existing provider/official-peer/authoring receipts establish their named models, transports and scenarios. Do not extrapolate to every discovered model or untested feature. Recheck actual full-suite failures before choosing the next change. Public API compatibility or a feature-parity score against OpenCode is expressly not required by architecture.md.

## Next implementation boundary after qualification

The complete installed-generation native gate has passed. Begin shared scheduling/agent placement work from the owner interfaces and the real AgentService/GraphService dispatch paths. Maintain backend-exclusive credentials, SQLite and effect authority; no worker may construct a second persistence writer or obtain authority from model-provided paths. Changes need actual mixed-root/process lifecycle tests, not a parser-only placement option. Continue packaging only verified reviewable source; preserve installed53 and the live TestProj owner while candidates evolve.
