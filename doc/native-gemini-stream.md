# Native Gemini streaming component

The C++ `GeminiStream` component decodes GenerateContent Server-Sent Events.
It accepts a single response candidate and retains the supplied model version,
response identity, original ordered part JSON and actual cumulative token
counters. JSON arguments retain their original numeric tokens; optional
provider call IDs and absent arguments remain absent.

Thought summaries and signature-only parts are retained in the wire-specific
history, outside ordinary text events. Signatures remain opaque and attached
to their original parts. The component does not merge signed parts, fabricate
usage totals, invent tool IDs or acknowledge an incomplete stream. Tool calls
are available only from a successful, validated `finish()`; output truncation
removes executable calls. Provider errors, blocked/unsupported finish reasons,
duplicate fields/IDs, changed response identity, multiple candidates,
unsupported media and excessive input fail explicitly with native diagnostics.

The implementation follows Google's
[GenerateContent REST reference](https://ai.google.dev/api/generate-content).
Its synthetic contract covers fragmented Unicode/SSE framing, EOF, signed
history, hidden thought separation, precise arguments, optional identities,
supplied usage and malformed/unsupported response rejection. Source adds a
59th required native contract. Compilation/execution of this new component
remains pending while the sibling runtime benchmark is active; the previous
58-contract gate does not verify it.

This is a stream component, not Gemini product availability. New native
[endpoint/auth transport source](native-gemini-provider.md) joins these components
but has not been compiled/executed. Request/response history adaptation, usage normalization, native
agent tool identity bridging, discovery, credential enrollment and live
provider acceptance remain unfinished. Request serialization uses its
[separate native component](native-gemini-request.md); signature-only response
parts do not yet have a request-history bridge. The Interactions API and media
are not implemented by this decoder.
