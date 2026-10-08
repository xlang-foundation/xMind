# Native Gemini request component

The new C++ component builds GenerateContent JSON with text turns, system
instructions, JSON Schema function declarations, correlated function results
and an optional output limit. It preserves argument/response numeric tokens and
opaque thought signatures. Provider call IDs remain optional and are never
invented. Unknown function capability, mismatched or incomplete results,
duplicates, malformed JSON and excessive input are rejected.

Model parts retain the optional `thought` flag, including the distinction
between absent and explicitly false. Signed thought summaries remain separate
from visible answer parts; the serializer preserves their order and signature
attachment. User/tool-result thought metadata is rejected. This is history
serialization only, not a reasoning display or streaming implementation.

The wire-specific DTO keeps model endpoint selection and credentials outside
the request body. It is not connected to the execution platform or provider
selector. Native transport, streaming/event translation, engine-history
adaptation, account discovery, protected credential enrollment and live model
acceptance remain pending. Media, caching, thinking controls and the separate
Interactions API also remain unimplemented.

The implementation follows Google's [GenerateContent REST reference](https://ai.google.dev/api/generate-content).
Its synthetic native contract checks text, schemas, signatures, precision,
function correlation and rejection cases. This source increases the required
complete native gate from 57 to 58 contracts. The local full gate passed all
58 contracts in 98.49 seconds, with 88 extension and 17 browser checks and the
actual native/browser integration contract also passing. See
[local provenance](evidence/native-gemini-local-provenance.json) and
[CTest output](evidence/native-gemini-local-ctest.log). Hosted validation of this
source remains pending. No Gemini availability or inference is claimed.
