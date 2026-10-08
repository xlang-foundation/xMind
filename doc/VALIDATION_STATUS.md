# Current native validation

The cleanup revision `dea588874e5a8c8e40bf1a158fa925520443c8a0` passed its
hosted gate: **58 native, 88 extension and 17 browser contracts**, native/browser
integration and VSIX verification, with zero failures/skips.
[Hosted run](https://github.com/xlang-foundation/xMind/actions/runs/37734970144),
[exact provenance](evidence/native-cleanup-hosted-provenance.json).
This verifies removal of the prototype/legacy tree, the native root build,
merged `doc/`, descriptor attribution, graph dependency filtering and the original
Gemini request component. It also verifies the repository error-boundary fix
that failed the earlier request gate.

Gemini transport/replay revision `ddd1d3da087f9ca7f8b0b8d81705c82632e15094`
also passed its separate hosted gate: **60 native, 88 extension and 17 browser
contracts**, native/browser integration and VSIX verification, with no
failures/skips. [Hosted run](https://github.com/xlang-foundation/xMind/actions/runs/37736323072),
[exact evidence](evidence/native-gemini-transport-hosted-provenance.json).
This verifies native request/SSE/HTTP components and expanded replay data;
it excludes the newer common gateway, history bridge and limit fixes below.

Newer working source passed **62 native contracts** locally in **100.44 seconds**.
It includes Gemini SSE/HTTP, expanded replay data, common model gateway, history
bridge, stronger callback ordering/usage assertions and escaped large-read
response-limit regressions. The new native agent contract executes two actual
file reads, closes/reopens xlang3 SQLite, replays exact signed receipts/results
through a new run, and rejects malformed/truncated/unoffered calls without effects.
All cases compiled and passed; there are no post-build exclusions in this gate.
The earlier 61-contract log retains its historical exclusions.
[Current local log/provenance](evidence/native-gemini-agent-local-provenance.json).

Current thin-client sources are unchanged from the **91 extension and 17 browser**
pass; source equality is recorded. Actual browser/native integration passed again
against the newly compiled server with disposable processes/database. Hosted
execution of this exact newer checkpoint remains required.

The [Gemini bridge](native-gemini-history.md) uses labelled synthetic component
and socket fixtures. Actual native AgentRunner/file/SQLite acceptance is now
verified with synthetic provider replies; live Gemini inference and product
enrollment remain unverified. Thin clients can display the new wire;
default discovery/enrollment remains unchanged.

The installed browser preview retains its separate verified native `19d69dd`
and view `6f32d215` snapshots. Neither the cleanup bundle nor this newer native
source is installed there. See [provider setup](provider-setup.md).

The Python prototype and original xlang runtime/service/plugin assets, launchers
and dependent probes are removed. Both root and Native CMake entry points use
the current native contracts. Generic xlang3 dependency setup remains separate
from the native agent engine. [Cleanup](cleanup.md).

Complete coding parity, broad native providers, remaining MCP/A2A capabilities
and Local/Nexus connection profiles remain active requirements. Team-server
implementation, PostgreSQL, WebRTC and Electron belong to private Nexus.
[Architecture](architecture.md), [parity baseline](PARITY.md).
