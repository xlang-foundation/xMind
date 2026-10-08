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

Newer working source passed **61 native contracts** locally in **103.05 seconds**,
**91 extension and 17 browser checks**, plus actual browser/native integration.
This includes Gemini SSE, native HTTP transport, expanded replay data, common
model gateway and the agent-history bridge. Additional callback ordering/usage
assertions and a function-response bound correction with escaped large-read
regressions were added after compilation; those changes await execution and are
excluded from the pass claim. A sibling runtime benchmark
prevents a local C++ rebuild. Hosted execution of this checkpoint is still
required. [Local log/provenance](evidence/native-gemini-history-local-provenance.json).

The [Gemini bridge](native-gemini-history.md) uses labelled synthetic component
and socket fixtures. No live Gemini inference, product enrollment or actual
Gemini AgentRunner execution is claimed. Thin clients can display the new wire;
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
