# Current native validation

Current source passed the complete local **66 native contracts in 114.49
seconds**, **98 extension and 17 browser tests**, with the exact expected native
manifest, zero failures/skips and no post-build exclusions. The new actual
Claude AgentRunner contract passed in **0.56 seconds**; its native provider
adapter contract passed in **0.23 seconds**. Two real native file reads and
correlated Messages tool IDs/results survive actual xlang3 SQLite close/reopen
and a new run's exact history replay without repeating earlier tools. Encrypted
provider credentials and terminal run ownership remain intact. Malformed,
unoffered, incomplete, length-truncated and late-error provider turns fail before
tool effects or successful assistant history. Held-stream cancellation and later
actual execution recover; injected tool-row SQL failure rolls back the
conversation batch while retaining observed real read outcomes.

Native Claude usage retains supplied raw input/output/cache counters and zeros,
adds common aliases without inventing totals, and marks input as uncached.
Actual shared DOM tests display live usage events and persisted transcript
metadata, separate cache writes/reads, safe zero counters, missing counts,
provider/model identity and backend-measured timing. Fresh browser/native
integration passed against the rebuilt server and source-matched assets using
disposable processes/database. The independent provider socket replies and
credentials are synthetic; these checks do not establish Claude
thinking/signatures, live Claude account acceptance, actual rendered IDE
acceptance or an installed-preview upgrade. Hosted validation of this newer
66-contract source remains pending.
[Exact local provenance](evidence/native-anthropic-agent-local-provenance.json),
[Claude agent scope](native-claude-agent.md).

The earlier `ac69c1f` CLI checkpoint separately passed the complete local
**65 native contracts in 113.16 seconds**, with its exact manifest, zero
failures/skips and no post-build exclusions. Its CLI contract took
**4.23 seconds**. It exercises
OpenAI, Claude and Gemini catalogue authentication, private environment-key
setup, saved-key updates, explicit profile selection and actual xlang3 SQLite
reopen with encrypted credentials. Signed Gemini text/history/usage, stale discovery/admission
without retry, safe native provider diagnostics and failed-turn status preserved
through settings until a later actual successful turn also passed. Public
profile/model key reflection is rejected across all three provider families.
Provider sockets and credentials are synthetic.
[Local CLI provenance](evidence/native-provider-profile-cli-local-provenance.json).
Its [hosted run](https://github.com/xlang-foundation/xMind/actions/runs/37746664258)
was last observed running; no hosted success is claimed here. This prior gate
excludes the newer Claude agent and renderer work above.

At that CLI checkpoint, unchanged thin-client sources retained the earlier
verified **94 extension and 17 browser tests**, with zero failures/skips; those
suites were not rerun for the
CLI change. Fresh browser/native integration passed against that rebuilt server
and source-matched assets, including four default routes, inactive Gemini
profile save, refresh/restart, real file-reading graph execution, history and
reconnect. No live Gemini inference, rendered IDE acceptance or installed-preview
upgrade is claimed. [CLI scope](native-interactive-cli.md),
[provider setup](provider-setup.md).

Enrollment revision `2f5e0f05e9bfadcf86b5508863da0ec5a9e78cfe` passed its
exact hosted **64 native, 94 extension and 17 browser contracts**, native/browser
integration and VSIX verification, with zero failures/skips. Its native gate
took **158.09 seconds**. It previously passed the local 64-contract gate in
**120.98 seconds**. Gemini discovery/pagination, generation-method filtering
separate from native tool policy, encrypted enrollment/selection, owned-key file
execution, cancellation, CAS rollback and signed SQLite replay passed using
synthetic peers. Direct-SQLite removal and shared `records.hpp` cleanup are
included. This hosted result excludes the newer generic CLI controls and
all-provider reflection guard.
[Hosted enrollment run](https://github.com/xlang-foundation/xMind/actions/runs/37743203538),
[exact hosted provenance](evidence/native-gemini-enrollment-hosted-provenance.json),
[earlier local scope](evidence/native-gemini-enrollment-local-provenance.json).

Gateway/history revision `75f45f0a036f1ffbab8c4b157df364f3697b52a0` passed the
exact hosted gate: **61 native, 91 extension and 17 browser contracts**,
native/browser integration and VSIX verification, with zero failures or skips.
Its native gate took **121.84 seconds**.
[Hosted run](https://github.com/xlang-foundation/xMind/actions/runs/37738970918),
[exact provenance](evidence/native-gemini-history-hosted-provenance.json).
This includes common Gemini gateway/history receipts, separate native/provider
IDs, normalized supplied usage, strengthened callback ordering/argument/usage
assertions, aligned replay limits and escaped large-response regressions. It
excludes later `c9591fe` agent acceptance and new catalogue/profile enrollment.

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

The subsequent `c9591fe` source passed **62 native contracts** locally in
**100.44 seconds**.
It includes Gemini SSE/HTTP, expanded replay data, common model gateway, history
bridge, stronger callback ordering/usage assertions and escaped large-read
response-limit regressions. The new native agent contract executes two actual
file reads, closes/reopens xlang3 SQLite, replays exact signed receipts/results
through a new run, and rejects malformed/truncated/unoffered calls without effects.
All cases compiled and passed; there are no post-build exclusions in this gate.
The earlier local 61-contract log retains its historical exclusions; the hosted
`75f45f0` gate above includes the later callback and large-response assertions.
[Local agent provenance](evidence/native-gemini-agent-local-provenance.json).

At that 62-contract milestone, thin-client sources matched the earlier **91
extension and 17 browser** pass; source equality is recorded. Actual browser/native
integration passed again against its compiled server with disposable
processes/database. Exact revision `c9591fe79cad9a4253ac8088933f0c8a2848ded1`
subsequently passed its hosted **62 native, 91 extension and 17 browser
contracts**, native/browser integration and VSIX verification, with no
failures/skips. Its native gate took **128.80 seconds**.
[Hosted run](https://github.com/xlang-foundation/xMind/actions/runs/37740505866),
[exact agent provenance](evidence/native-gemini-agent-hosted-provenance.json).
Neither the earlier hosted 61 gate nor the local/hosted 62 gates cover new
catalogue/enrollment source.

The [Gemini bridge](native-gemini-history.md) uses labelled synthetic component
and socket fixtures. Actual native AgentRunner/file/SQLite acceptance is now
verified with synthetic provider replies, including the new native
discovery/enrollment source in the local/hosted 64-contract gates and the generic
CLI boundary in the prior local 65-contract gate. The current local 66-contract
gate additionally verifies actual Claude AgentRunner/file/SQLite acceptance and
its shared metrics renderer at the scoped synthetic-provider/DOM boundary.
Live Gemini and Claude inference remain
unverified.

The installed browser preview retains its separate verified native `19d69dd`
and view `6f32d215` snapshots. Neither the cleanup bundle nor this newer native
source is installed there. See [provider setup](provider-setup.md).

The Python prototype and original xlang runtime/service/plugin assets, launchers
and dependent probes are removed. Both root and Native CMake entry points use
the current native contracts. Generic xlang3 dependency setup remains separate
from the native agent engine. [Cleanup](cleanup.md).
The final 64-contract build also removes the unused direct-SQLite store and
contract; shared record types now live in `records.hpp`. Production database I/O
continues through embedded xlang3.

Complete coding parity, broad native providers, remaining MCP/A2A capabilities
and Local/Nexus connection profiles remain active requirements. Team-server
implementation, PostgreSQL, WebRTC and Electron belong to private Nexus.
[Architecture](architecture.md), [parity baseline](PARITY.md).
