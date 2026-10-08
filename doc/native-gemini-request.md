# Native Gemini request serialization

The C++ component builds GenerateContent JSON with text turns, leading system
instructions, JSON Schema function declarations, correlated function results
and an optional output limit. It preserves argument/response numeric tokens,
opaque thought signatures and original `partMetadata` JSON. Provider call IDs
and function arguments remain optional; absent values are not invented.

Model parts retain the optional `thought` flag, including absent versus false,
signature-only parts and empty text. Signed summaries remain separate from
visible answer parts, in their original order. User/tool-result thought metadata,
unknown function capability, mismatched or incomplete results, duplicate fields,
malformed JSON and excessive input are rejected. Model turns allow up to 4096
parts, with at most 64 pending calls; serialized requests are bounded to 8 MiB.

The implementation follows Google's
[GenerateContent REST reference](https://ai.google.dev/api/generate-content).
Request serialization is now connected to the native
[HTTP/SSE adapter](native-gemini-provider.md) and
[common agent-history bridge](native-gemini-history.md).

The local working source passed the complete **62-contract native gate**,
including expanded replay and escaped large-read cases. Function-response
objects may use the 8 MiB body budget; arguments, schemas and replay metadata
retain 1 MiB/depth-16 limits. The complete serialized request remains bounded to
8 MiB. See [local scope](evidence/native-gemini-agent-local-provenance.json).
The original request component and repository error-boundary correction also
passed the hosted cleanup gate at `dea5888`: **58 native, 88 extension and 17
browser contracts**, with zero failures or skips.
[Hosted provenance](evidence/native-cleanup-hosted-provenance.json).
That older hosted gate excludes the later stream, transport and history source.

Expanded signature-only/empty/metadata replay DTOs subsequently passed the hosted
**60-contract gate** at `ddd1d3d`, alongside native streaming and transport.
[Exact evidence](evidence/native-gemini-transport-hosted-provenance.json).
It excludes the newer history bridge, gateway and limit corrections.

Gemini is not yet enrolled through product Settings or model discovery. No live
Gemini inference is claimed. Media, caching controls, thinking controls, Vertex
AI/OAuth and the separate Interactions API remain unimplemented.
