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

The newer serializer separates function-response objects (up to the 8 MiB body
bound) from 1 MiB arguments/schemas/metadata, so escaping an allowed large native
file-read result does not inherit the smaller argument bound. Its regression
checks were added after the local gate compiled and still require execution.

Usage normalization retains supplied Gemini counters and maps actual prompt,
candidate, total, cached and thought counts to common fields. It neither estimates
missing counters nor adds prompt and completion counts to fabricate a total.

The history contract uses labelled synthetic completions and an AgentRunner-shaped
JSON persistence round trip. It covers precision, signatures, absent arguments,
metadata, provider/native identity separation, reverse tool-result arrival,
hidden thoughts, empty STOP, truncation, corrupt receipts, failing ID factories
and limits. The independent socket contract exercises the common gateway's
signed continuation. Both passed in the local **61-contract gate**.
[Evidence and post-build assertion scope](evidence/native-gemini-history-local-provenance.json).
This verifies the native bridge and transport, not actual Gemini AgentRunner
execution, Settings enrollment, model discovery or live inference. Those remain
required acceptance work.
