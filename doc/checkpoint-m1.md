# M1 checkpoint: native persistence

This checkpoint contains the native C++ repository, embedded-xlang3 SQLite adapter, ownership lease, Windows secret protection component, persistence demo, contracts and architecture. Historical Python/backend and editor scaffolding are documented as prototypes; full coding, models, MCP/A2A, graphs and team deployment remain incomplete.

## Runtime prerequisite

Use CantorAI/xlang3 revision `914783909835116969aad7c66b210a5ac9a27661` with the SQLite module changes in `runtime-prerequisites/sqlite-xlang3.patch`. These changes were tested on branch `agentflow/mcp-native-compat`; unrelated pending cryptography changes are excluded from the patch. From that runtime checkout, apply the patch with `git apply`, build target `xlang_sqlite3_native_package` in Release, and run the included `isolation_and_text.py` fixture with xlang3 and the standard-library source directory configured. No CPython interpreter is used.

The patch adds the isolation-level API needed for explicit transactions and fixes SQL string lifetime and embedded-NUL text handling. It changes the native SQLite module, not the generic VM. The full generic performance gate has not been claimed for this module checkpoint.

## Reproduce the milestone

From the xMind checkout, use `Tools/native-milestone.ps1 -Action Build`, then `-Action Seed` and `-Action Read`. Supply `-RuntimeDirectory` and `-PythonLibSource` when their locations differ from this development machine. Seed refuses an existing populated database. Read reopens persisted state; `-After 2` replays only later events.

On October 6, 2026, all three native contracts passed again: Windows protection, embedded SQLite values/transactions and domain repository lifecycle/rollback/recovery. Earlier repeated runs and separate seed/read processes are recorded in `milestones.md`. Generated databases and build outputs are excluded from Git.

## PostgreSQL status

Historical PostgreSQL investigation (now excluded from OSS and moved to Cantor Nexus): only SQLite has a working adapter. The private `requirements-postgres-pure.lock` pins the candidate pure-Python PostgreSQL stack. Installing it with xlang3 pip failed before driver imports with `TypeError: memoryview() requires a bytes-like object`, surfaced through pip's candidate iterator. The underlying call site still needs diagnosis; this is not evidence of a pg8000 defect or a confirmed missing native API. No interpreter substitution or runtime workaround was used. No development PostgreSQL service was found on this PC.
