# Native Gemini agent-history bridge

`gemini_model_request` and `gemini_model_completion` connect the common native
message/completion contracts to GenerateContent. This keeps agent execution,
tool authorization and SQLite ownership in the existing C++/xlang3 backend.

Each completion stores a `gemini_content` provider receipt containing the exact
ordered parts as a JSON string, finish reasons and native-call-to-part bindings.
The string survives the agent's JSON envelope and repository round trips without
rewriting numeric tokens, opaque signatures, metadata or field presence. Provider
call IDs remain provider data; every executable call receives a separately
allocated native ID. Same-name calls without provider IDs remain distinguishable.

Replay checks the receipt against the assistant's visible content and tool calls,
rejecting altered, foreign, missing or duplicate identities/results. Tool answers
are grouped into a user turn in original part order, even if answers arrive in
reverse order. Function responses carry the original provider ID when present
and wrap the actual result string in `{"output": ...}`. Leading system messages
remain system instructions. Unsupported developer/refusal/foreign history fails
explicitly. Truncated tool-call turns cannot be replayed as executable calls.

Thought summaries remain outside ordinary answer text. Signature-only and empty
model text parts retain their presence. A valid zero-part STOP receipt persists,
then contributes no unsigned empty model turn to the next request. The bridge
enforces 4096 model parts, 64 calls, 8 MiB input limits and bounded JSON objects.

The serializer separates function-response objects (up to the 8 MiB body
bound) from 1 MiB arguments/schemas/metadata, so escaping an allowed large native
file-read result does not inherit the smaller argument bound. Its regression
checks passed both the `75f45f0` hosted 61-contract gate and the newer `c9591fe`
local 62-contract gate.

Usage normalization retains supplied Gemini counters and maps actual prompt,
candidate, total, cached and thought counts to common fields. It neither estimates
missing counters nor adds prompt and completion counts to fabricate a total.

The history contract uses labelled synthetic completions and an AgentRunner-shaped
JSON persistence round trip. It covers precision, signatures, absent arguments,
metadata, provider/native identity separation, reverse tool-result arrival,
hidden thoughts, empty STOP, truncation, corrupt receipts, failing ID factories
and limits. The independent socket contract exercises the common gateway's
signed continuation. These component/gateway cases passed the exact
`75f45f0a036f1ffbab8c4b157df364f3697b52a0` hosted **61-contract gate in 121.84
seconds**, with **91 extension and 17 browser checks**, native/browser integration
and VSIX verification, with no failures/skips.
[Hosted scope](evidence/native-gemini-history-hosted-provenance.json).
They also passed in the later local **62-contract gate**.
[Local agent evidence](evidence/native-gemini-agent-local-provenance.json).

The new independent agent contract also executes actual native file reads,
records their tool results through xlang3 SQLite, closes/reopens the database and
starts a new run whose real AgentRunner loads and replays the saved receipts.
It verifies native/provider ID separation, exact signatures/metadata, supplied
usage, measured timings and failure without effects. Provider replies are
synthetic; this verifies the `c9591fe` native agent/file/persistence milestone,
not live Gemini inference. Exact revision
`c9591fe79cad9a4253ac8088933f0c8a2848ded1` also passed its hosted **62 native,
91 extension and 17 browser contracts**, native/browser integration and VSIX
verification, with no failures/skips and **128.80 seconds** for the native gate.
[Hosted agent evidence](evidence/native-gemini-agent-hosted-provenance.json).

New source adds authenticated native catalogue
discovery, encrypted profile enrollment and model-specific backend tool policy;
the expanded **64-contract gate passed locally in 120.98 seconds**, with zero
failures/skips, an exact expected manifest and no post-build exclusions. Enrolled
native agents passed actual file execution, supplied metrics, cancellation and
signed history replay after xlang3 SQLite reopen. This keeps generation-method
eligibility separate from tool capability and retains the same signed receipt
boundary for enrolled agents.
[Current local evidence](evidence/native-gemini-enrollment-local-provenance.json).
Hosted validation of this newer checkpoint remains pending.
[Provider setup](provider-setup.md). No external
Gemini key, live inference or installed-preview upgrade is claimed.
