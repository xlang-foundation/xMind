# Native model-stream adapter

`ChatCompletionStream` is the incremental C++ decoder for Chat Completions SSE bytes. It is a real wire-protocol component for the native provider layer, not a simulated model, HTTP transport or agent engine. No product execution route has been enabled by adding it.

The contract was checked against the official [streaming event schema](https://developers.openai.com/api/reference/resources/chat/subresources/completions/streaming-events) and [request reference](https://developers.openai.com/api/reference/resources/chat/subresources/completions/methods/create), accessed October 6, 2026. Model names and availability are not inferred from those schemas.

The adapter handles fragmented SSE bytes, CR/LF/CRLF boundaries, initial UTF-8 BOM, comments and multiline data. It reconstructs one completion choice, text, refusal, separately indexed function calls and final usage. Extensions in provider deltas remain distinct `model.extension` events rather than silently become answer text. Provider-specific continuation still needs the appropriate adapter.

`finish()` returns a result only after a finish chunk and the provider's end marker. Unknown finish reasons, malformed JSON/types, changing response/tool identities, duplicate final tool identities and incomplete streams fail explicitly. Complete tool arguments must be a JSON object. Length/filtered output never returns executable tool calls. Callers must still check finish reason, tool identity against the registered schema, policy and authorization before executing anything. A `model.finish` event alone is not run completion.

Bounds are 1 MiB per SSE line, 4 MiB per event/text/arguments value, 64 MiB per response and 64 tool indices. Consumer exceptions abort and invalidate decoding. Error diagnostics do not echo provider error bodies or malformed JSON. The adapter has no automatic retry; a partially emitted response cannot be silently replayed as a new response.

## Evidence and remaining work

`model_stream_protocol_contract` uses explicitly synthetic protocol fixtures. It tests every split position of a tool/text/usage stream, individual-byte delivery including UTF-8, parallel tool fragments, extension preservation, malformed input, bounds, truncation, unknown finish reasons and consumer failure. This proves decoder behavior only; no real model was called. Full native build evidence: [model-stream-ctest.log](evidence/model-stream-ctest.log).

Native HTTPS transport, request serialization/capability validation, provider credential resolution, real model validation, model/tool orchestration, cancellation, retries and all other provider families remain outstanding. The selected endpoint/model and private credentials are needed for live validation; no supported-model claim is made by this component. OpenAI Responses, Anthropic, Google and other protocols require their own native adapters against the existing LiteLLM coverage reference.
