# Native Gemini request/transport boundary

`complete_gemini` joins the C++ GenerateContent request serializer, Windows
HTTP event-stream transport and Gemini SSE decoder. It binds a backend-selected
model resource into a versioned endpoint base and appends `alt=sse`; model and
credentials are not copied into the body. API-key authentication uses the
backend-owned `SecretBytes` through the `x-goog-api-key` header.

The existing transport verifies OS certificates for HTTPS, permits plain HTTP
only for explicitly selected loopback peers, enforces deadline/cancellation
and refuses redirects or automatic retries. The new boundary withholds final
completion events until EOF and requested-tool validation succeed. It never
executes a tool. Returned history, optional call identities, exact arguments
and supplied usage retain their wire-specific representation.

The endpoint follows the
[GenerateContent streaming reference](https://ai.google.dev/api/generate-content);
header authentication follows Google's
[API-key documentation](https://ai.google.dev/gemini-api/docs/api-key).
The independent synthetic socket contract covers header/body/resource binding,
signed tool-result continuation, fragmented response bytes, unoffered calls,
incomplete/late-error/blocked responses, HTTP failures, no redirect/retry and
cancelled/invalid admissions. This source adds the 60th required native contract.
The peer passed JavaScript syntax validation; new C++ compilation and actual
socket execution remain pending. Earlier 58-contract evidence excludes the
Gemini stream and transport components.

This adapter is not yet registered in the agent/model gateway's provider-wire
selection or saved provider profiles. Agent history adaptation, optional-call
identity bridging, signature-only history replay, usage normalization,
discovery/enrollment and live provider acceptance remain unfinished. Gemini is
not available in product model selectors. Vertex AI/OAuth and the Interactions
API are separate, unimplemented routes.
