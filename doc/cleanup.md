# Native source-tree cleanup

The maintained repository is `D:\CantorAI2026\xMind`. Its source layout is:

| Path | Purpose |
| --- | --- |
| `Native/` | C++ backend, CLI, xlang3 SQLite integration and native contracts |
| `extensions/vscode/` | Thin VS Code host and shared conversation renderer |
| `views/browser/` | Browser view and its access adapter |
| `Tools/` | Current launchers, build helpers and reference/verification tools |
| `doc/` | Architecture, setup, capability references and verification records |

The earlier Python agent prototype and original xlang Core, CLI, services,
`DevSrv_Plugins`, configuration, examples, schemas and Docker setup were removed.
The unsupported direct-SQLite store and its optional build were removed too.
Root CMake delegates to `Native/`; product database I/O uses embedded xlang3.
Git history and applicable license notices preserve upstream provenance.

All maintained documentation is under `doc/`; `Documents/` is absent.
The two development guides were consolidated into [DEVELOPMENT.md](DEVELOPMENT.md),
with a [documentation index](README.md) for current entry points.

The final cleanup pass removes the unused FastAPI bootstrap `Tools/setup.ps1`,
`requirements-pure.lock` and `Tools/probes/xlang3_dependencies.py`. It also
removes the old `Tools/mcp-peer` HTTP peer for the retired Python backend.
None was referenced by the native build, CI or current clients. Official SDK
stdio contracts under `Native/tests/sdk` remain supported test inputs.
Future pure-Python libraries may still be installed and executed under xlang3
when required by the final architecture.

Automatic approval review rejected removal of the ignored
`Tools/mcp-peer/node_modules` cache with "blocked by policy". The obsolete
tracked peer files are removed; that dependency cache remains on disk.

Current native dependencies/licenses, meaningful fixtures, historical evidence,
provider/parity inventories and the SQLite runtime prerequisite are retained.
Running backend/view snapshots, SQLite sessions, encrypted credentials and
uncommitted native feature work are preserved. Ignored build/runtime caches
are not product source.

The separate `D:\CantorAI2026\AgentFlow` folder contains stale generated build
artifacts, not a second source checkout. It is not empty. An earlier automatic
approval review rejected its removal with "blocked by policy"; no deletion
workaround was used. Current source, builds and launchers use xMind.

Team-server, PostgreSQL, WebRTC and Electron implementation belongs to the
sibling private Nexus repository. OSS retains the generic Local/Nexus protocol
boundary and does not package those private components.

Earlier cleanup revision `dea5888` passed its hosted 58-native/88-extension/
17-browser gate; [exact historical evidence](evidence/native-cleanup-hosted-provenance.json)
retains that result. This later cleanup changes unused tooling and documentation,
not the native runtime. Its validation consists of tracked-reference, layout,
documentation-link and Git diff checks; it does not claim a new native test pass.
