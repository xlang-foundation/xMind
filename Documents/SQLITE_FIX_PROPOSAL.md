# SQLite SQL text lifetime investigation

Current status: the user authorized runtime fixes on a branch. Implemented in `D:\CantorAI2026\xlang3` on `agentflow/mcp-native-compat`: compatible connect isolation-level options/properties and implicit/explicit transaction selection; owned SQL text through cursor execution; length-aware text binding/reading. Built the SQLite native module in Release. Existing legacy/Python API fixtures and new isolation/binary-text fixture passed. The AgentFlow native adapter/repository contracts passed ten repeated runs, and seed/read replay passed. No generic VM/runtime source changed. Earlier broader Python engine failures are not claimed resolved by these native checks.

The native C++ embedding tests now also confirm `sqlite3.connect(..., isolation_level=None)` is rejected. Source `sqlite3_connect_kw` currently accepts only `check_same_thread`, and `begin_python_transaction` unconditionally begins a default transaction for writes when SQLite is in autocommit. The C++ adapter requests `None` so it can own explicit transactions. Removing the keyword would change that contract and is not an approved workaround.

Additional proposed fix: implement validated isolation-level configuration (None for explicit transaction ownership; compatible supported string modes for implicit transactions), retain default behavior and validate error cases. Apply together with the owned SQL-text fix described below on the isolated native branch, after the user's decision. Keep this within `modules/sqlite` and its fixtures; no generic VM/runtime changes are proposed. Actual connection options and explicit/implicit transaction behavior need native tests through xlang3; SDK keyword acceptance alone is insufficient.

Observed failures: existing store/engine checks intermittently report `UPDATE` affected rows as `-1`, while simpler cursor probes pass. Diagnostic evidence is preserved in `.agentflow/engine-revalidation.txt`. `tests/sqlite_rowcount_probe.py`, `tests/sqlite_temporary_cursor_probe.py`, and `tests/sqlite_sql_lifetime_probe.py` passed and do not by themselves reproduce the broader failure.

Source evidence in the pinned xlang3 checkout:

- `src/api/c_api.cpp`, `x3_value_to_cstr`: returns a pointer into a shared thread-local string, replaced on subsequent conversions.
- `modules/sqlite/sqlite_values.cpp`, `require_string`: obtains the SQL pointer through that conversion API.
- `modules/sqlite/sqlite_package.cpp`, `cursor_execute`: keeps the SQL pointer while `bind_params` converts text arguments through the same API, then reads the SQL pointer again through `is_write_sql` and insert/replace classification.

The pointer lifetime is unsafe by inspection. It is a plausible cause of the intermittent affected-row failures, not yet a verified explanation of every failure.

Proposed native fix: create an owned `std::string` copy immediately after SQL validation, and use its `c_str()` for the rest of `cursor_execute`. Audit other SQLite call paths for the same lifetime pattern. This remains scoped to the SQLite native module and does not require modifying generic VM/runtime code.

User discussion is pending before changing native source, following the instruction to discuss missing native capabilities or workarounds first. If approved, implement on `agentflow/mcp-native-compat`, preserve the fixed Release baseline, validate the full previously failing store/engine tests repeatedly and affected SQLite fixtures, and retain failure diagnostics until the cause is confirmed. Coordinate resource-heavy builds with the other active benchmark. Do not weaken transition checks or replace xlang3 with CPython.
