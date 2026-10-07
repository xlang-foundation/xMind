# Current validation status

## Current native checkpoints — October 7, 2026

The latest verified native Release evidence is [eighteen passing contracts](../doc/evidence/native-approval-api-ctest.log): embedded-xlang3 SQLite persistence and encrypted credentials, authenticated local server/CLI, scheduling/model streaming/read tools, durable approvals, and actual approved file application. These tests use synthetic inference peers and real filesystem/storage; they do not prove live-provider or end-to-end coding completion.

The latest verified VS Code adapter evidence is [nineteen passing client/host/renderer contracts](../doc/evidence/vscode-reconnect.log). The real right-sidebar layout was inspected in a signed official VS Code host. Exact edit snapshot comparison, reconnect and model preference persistence have fixture coverage; live model-driven approval/diff interaction remains unverified.

Native model-invoked writes, per-run model catalogue/selection, persisted response usage/timings, and expanded bounded-parser checks are uncommitted work awaiting build/execution. Observed live xlang3 benchmarks defer the native build gate. MCP/A2A, graphs, complete coding tools, all provider coverage, team authorization/PostgreSQL and Electron remain required. See [milestones](../doc/milestones.md) and [architecture](../doc/architecture.md).

## Historical validation record

The paragraphs below preserve earlier checkpoints and failures. They are not the current native result; use the dated scope above and its evidence links.

Latest verified milestone: native C++ persistence through embedded xlang3. User-authorized SQLite fixes on the isolated native branch rebuilt successfully. All three default native contracts passed ten consecutive runs; independent seed/read processes preserved session/messages/configuration and exact event cursor replay. Legacy SQLite fixtures and the new isolation/binary-text fixture passed. See `../doc/milestones.md`. Earlier failure paragraphs below are historical evidence, not current native-test results. Runtime fixes remain uncommitted; full platform/protocol/UI/parity scope is incomplete.

Native Windows credential protection compiled and passed its component contract. SQLite integration remains pending. Current full native test result: one pass (protection), two failures (`isolation_level` rejection). Evidence: `build/native/evidence/contracts-20261006.log`. This is not a persistence milestone or whole-platform pass.

Default native targets compiled in Release. The embedded adapter and repository tests both failed at `sqlite3.connect(..., isolation_level=None)`: this keyword is not supported by the current xlang3 SQLite binding. No native persistence milestone is delivered. Runtime changes and workarounds remain pending user discussion; see `SQLITE_FIX_PROPOSAL.md` and `../doc/milestones.md`. This supersedes earlier statements that the default targets were uncompiled.

Latest persistence direction: all SQLite operations through embedded xlang3. `XlangSqlite` source and its native contract test are now wired into the default standalone CMake target, but remain uncompiled/unverified. The older direct-SQLite store and contracts are disabled references, not target implementation evidence. Secret/general configuration storage, native session repository and local/remote server clients remain required work.

Architecture changed by user request: core implementation must be native C++. Python/FastAPI results below are historical prototype results, not native acceptance evidence. The native core, service, CLI, provider adapters and protocol/graph implementations remain outstanding; see `NATIVE_ARCHITECTURE.md`.

Native store and backend-lease source, independent CMake target and contract tests are now prepared. They remain uncompiled and unverified while the other checkout's live benchmark runs. Tests include transaction faults, competing connections and an independent process lease probe. See `../doc/native-development.md`. No native pass is claimed yet.

All Python checks execute with the cloned xlang3 Release binary, using pure-Python packages from `.agentflow/site-packages`. Do not substitute CPython or weaken acceptance checks.

## Newly verified

`tests/coding_tools_probe.py` passed: literal workspace search, ignored repository metadata, exact unique replacement, ambiguous replacement rejection and write permissions. `tests/mcp_server_probe.py` passed after adding these tools.

## Revalidation failures

The expanded graph probe fails before node execution with a closure-scope error in a generator nested inside a list comprehension (`visited` is reported undefined). `tests/nested_comprehension_probe.py` isolates this. User discussion is pending for either coordination with the other xlang3 chat or explicit-loop application code.

Repeated existing engine/store checks also show intermittent failures: transition affected-row checks failed on some runs; a later engine check observed a completed run but the last event lacked expected text. These are not yet isolated to an application or runtime defect. Earlier passes do not establish current reliability. Assertions remain intact and now include more diagnostic event/state information. The simple `tests/sqlite_rowcount_probe.py` passed; this does not resolve the broader failures.

Source inspection found an unsafe SQL pointer lifetime in SQLite's native `cursor_execute`: parameter conversion replaces the shared thread-local buffer backing the retained SQL pointer, which is subsequently used for write classification. This is a plausible cause; smaller probes still pass, so the full failing checks must confirm any eventual fix. See `SQLITE_FIX_PROPOSAL.md`. Native source remains unchanged pending the user's decision.

## Native branch

AESGCM source/tests exist in the sibling xlang3 branch, but compilation/runtime verification remain pending. Live benchmark processes in the other checkout are still active, so no competing heavy build has started.

## Incomplete scope

Graph runtime tests and HTTP graph execution, live outbound model/MCP networking, A2A, interactive permissions, snapshots/recovery, bounded loops, IDE validation and full OpenCode parity remain incomplete. The goal is active.
