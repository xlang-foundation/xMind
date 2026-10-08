# Native source-tree cleanup

The active checkout is `D:\CantorAI2026\xMind`; its maintained sources and
documentation use the native layout below. The separate
`D:\CantorAI2026\AgentFlow` directory was inspected and is empty. Automatic
approval review blocked its nonrecursive removal with the reason "blocked by
policy", so the empty directory remains; it is not another source checkout.

Removed the earlier Python AgentFlow implementation, its dependent probes and launcher, and the original xlang-based Core/CLI/service/debug plugins, configuration, examples, schemas and Docker setup. Original programming guides, legacy dependency lock and migration instructions were also removed. Git history retains the code and its license provenance.

Root CMake delegates to Native/. Cleanup revision `dea5888` passed its complete hosted gate: 58 native, 88 extension and 17 browser contracts, native/browser integration and VSIX verification, with no failures/skips. [Exact evidence](evidence/native-cleanup-hosted-provenance.json). Later Gemini agent/SQLite acceptance increased the manifest to 62; its full local gate passed in 100.44 seconds. Its [separate validation scope](VALIDATION_STATUS.md) does not change the cleanup evidence. The OpenCode inventory was regenerated with 136 upstream operations, zero removed-prototype mappings and 23 native source candidates; none is claimed verified parity.

The current clients, native dependencies/licenses, runtime prerequisites, verification evidence and generic xlang3 dependency setup remain. Running backend/view snapshots, SQLite state and encrypted credentials were preserved. Old bytecode was moved out of the importable root package into ignored local history; the unused virtual environment was moved outside the xMind checkout.

All maintained documentation and provider/parity inventories are now under
`doc/`; `Documents/` was removed. Audit tools write to that same directory.
The remaining generic dependency probe lives under `Tools/probes/`, leaving
native product contracts under `Native/tests/` and client checks with their
respective clients.

The disabled direct-SQLite implementation, optional CMake build and obsolete
contract were also removed. Its shared session/run/event/error records remain
in `Native/include/agentflow/records.hpp`, used by the current xlang3-backed
repository. The old license-insertion script targeting `D:/source/xMind` was
unused and removed; existing license notices and dependency licenses remain.
