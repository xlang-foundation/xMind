# Native Gemini transport and model gateway

`complete_gemini` joins C++ GenerateContent serialization, Windows HTTP
event-stream transport and Gemini SSE decoding. A backend-selected model
resource is bound into a versioned endpoint base with `alt=sse`. Authentication
uses backend-owned `SecretBytes` through the `x-goog-api-key` header.

The transport verifies OS certificates for HTTPS, permits plain HTTP only for
explicit loopback peers, enforces deadlines/cancellation and refuses redirects
or automatic retries. Completion is withheld until EOF and offered-tool
validation succeed. The adapter never executes tools.

The common `complete_model` gateway now accepts the
`gemini_generate_content` wire. The [history bridge](native-gemini-history.md)
allocates independent native tool identities, persists checked signed receipts,
replays correlated results and maps supplied usage to common token counters.
Common tool callbacks are emitted after validation, before finish/done.
Unsupported reasoning controls and unknown required capabilities fail explicitly.

The endpoint follows Google's
[GenerateContent reference](https://ai.google.dev/api/generate-content), with
[header authentication](https://ai.google.dev/gemini-api/docs/api-key).
An independent synthetic socket peer verifies header/body/resource binding,
signed continuation, exact arguments, optional identities, fragmented bytes,
unoffered calls, incomplete/late-error/blocked responses, HTTP errors,
cancellation and no redirect/retry. The common gateway also resumes a signed
tool turn through the persisted JSON envelope used by agent history.

The local complete **61-contract native gate** passed the transport and gateway
checks. Additional callback ordering/usage assertions were added after that
target compiled and await a rebuild/hosted execution; they are not included in
the local pass claim. The later function-response bound correction and its
large-read regressions also await compilation/execution. See
[exact scope](evidence/native-gemini-history-local-provenance.json).

The earlier transport/replay revision `ddd1d3d` passed the complete hosted
**60-contract gate**, with **88 extension and 17 browser checks**, native/browser
integration and VSIX verification. No failures/skips occurred.
[Hosted evidence](evidence/native-gemini-transport-hosted-provenance.json).
That exact source excludes the common gateway/history bridge and later limit
corrections; it establishes the typed request/HTTP/SSE component scope only.

Default product discovery/enrollment routes still expose OpenAI Chat/Responses
and Claude. Gemini discovery is explicitly rejected until its own catalogue
adapter exists; it does not reuse OpenAI model-list parsing. Thin clients can
render an advertised Gemini wire, but Settings does not yet enroll it. No actual
AgentRunner Gemini execution or live Gemini inference is claimed by these
component/socket checks. Vertex AI/OAuth and Interactions remain unimplemented.
