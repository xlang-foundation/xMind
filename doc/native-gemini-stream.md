# Native Gemini streaming

The C++ `GeminiStream` component decodes GenerateContent Server-Sent Events.
It accepts a single response candidate and retains supplied model version,
response identity, original ordered part JSON and actual cumulative token
counters. JSON arguments retain their numeric tokens; optional provider call
IDs and absent arguments remain absent.

Thought summaries and signature-only parts stay in provider history, outside
ordinary text events. Signatures remain opaque and attached to their original
parts. Tool calls become available only from a validated `finish()`; output
truncation removes executable calls. Provider errors, blocked/unsupported finish
reasons, duplicate fields/IDs, changed response identity, multiple candidates,
unsupported media and excessive input fail explicitly. The decoder limits raw
part history to 8 MiB and aligns object depth/size with request replay limits.

The implementation follows Google's
[GenerateContent REST reference](https://ai.google.dev/api/generate-content).
Its synthetic native contract covers fragmented Unicode/SSE framing, EOF,
signed history, precise arguments, optional identities, supplied usage and
malformed/unsupported response rejection. It passed in the local complete
**61-contract native gate**; see
[local evidence](evidence/native-gemini-history-local-provenance.json).
The earlier transport/replay revision `ddd1d3d` passed its full hosted
**60-contract gate**, with **88 extension and 17 browser checks**, no
failures/skips, native/browser integration and VSIX verification.
[Exact evidence](evidence/native-gemini-transport-hosted-provenance.json).
It includes this decoder and expanded replay DTOs, but excludes the newer common
gateway/history bridge and limit corrections.

The [native transport](native-gemini-provider.md) and
[history bridge](native-gemini-history.md) connect this decoder to `complete_model`.
This does not establish product enrollment or live provider acceptance. Media
and the Interactions API remain outside the implemented decoder scope.
