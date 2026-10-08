# Native source-tree cleanup

Removed the earlier Python AgentFlow implementation, its dependent probes and launcher, and the original xlang-based Core/CLI/service/debug plugins, configuration, examples, schemas and Docker setup. Original programming guides, legacy dependency lock and migration instructions were also removed. Git history retains the code and its license provenance.

Root CMake delegates to Native/. The configured root manifest still contains all 58 native contracts; the preceding complete native gate and current browser/extension checks passed at their documented scope. The OpenCode inventory was regenerated with 136 upstream operations, zero removed-prototype mappings and 23 native source candidates; none is claimed verified parity.

The current clients, native dependencies/licenses, runtime prerequisites, verification evidence and generic xlang3 dependency setup remain. Running backend/view snapshots, SQLite state and encrypted credentials were preserved. Old bytecode was moved out of the importable root package into ignored local history; the unused virtual environment was moved outside the xMind checkout.

All maintained documentation and provider/parity inventories are now under
`doc/`; `Documents/` was removed. Audit tools write to that same directory.
The remaining generic dependency probe lives under `Tools/probes/`, leaving
native product contracts under `Native/tests/` and client checks with their
respective clients.
