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

The local `c9591fe` **62-contract native gate** passed transport, gateway,
callback ordering/native-ID/exact-argument/usage and escaped large-read checks.
It also exercised actual native AgentRunner file execution and signed history
replay after xlang3 SQLite reopen, with an independent synthetic provider peer.
[Exact local scope](evidence/native-gemini-agent-local-provenance.json).
Exact agent revision `c9591fe79cad9a4253ac8088933f0c8a2848ded1` subsequently
passed its hosted **62 native, 91 extension and 17 browser contracts**, plus
native/browser integration and VSIX verification, with no failures/skips. The
native gate took **128.80 seconds**. It still excludes new catalogue/enrollment.
[Hosted agent scope](evidence/native-gemini-agent-hosted-provenance.json).

Gateway/history checkpoint `75f45f0a036f1ffbab8c4b157df364f3697b52a0` also passed
the exact hosted **61-contract gate in 121.84 seconds**, with **91 extension and
17 browser checks**, native/browser integration and VSIX verification, and no
failures/skips. It includes the common history bridge, callback assertions,
aligned replay bounds and escaped large-response correction, but excludes the
later actual AgentRunner acceptance and catalogue/profile enrollment targets.
[Hosted gateway/history evidence](evidence/native-gemini-history-hosted-provenance.json).

The earlier transport/replay revision `ddd1d3d` passed the complete hosted
**60-contract gate**, with **88 extension and 17 browser checks**, native/browser
integration and VSIX verification. No failures/skips occurred.
[Hosted evidence](evidence/native-gemini-transport-hosted-provenance.json).
That exact source excludes the common gateway/history bridge and later limit
corrections; it establishes the typed request/HTTP/SSE component scope only.

New source adds an independent Gemini catalogue adapter and authenticated native
profile enrollment to the default route policy. Discovery follows Google's
[models-list format](https://ai.google.dev/api/models), using header authentication,
fixed page size, opaque encoded cursors and full `models/<id>` identities. It
filters for GenerateContent method eligibility, with separate backend model tool
policy for workspace execution. Keys remain in encrypted xlang3 SQLite-backed
credentials; discovery, enrollment and selection use registry revision checks.
The expanded **64-contract native gate passed locally in 120.98 seconds**, with
an exact expected manifest and no post-build exclusions. Current thin-client
checks passed **94 extension and 17 browser tests**, with zero failures/skips.
Actual browser/native integration passed against the rebuilt four-route server,
including inactive Gemini profile persistence. These fixtures validate real
discovery, enrollment, agent/file execution, signed history and xlang3 SQLite
reopen; they do not establish live Gemini account acceptance.
[Current local evidence](evidence/native-gemini-enrollment-local-provenance.json).
Hosted validation of this newer checkpoint remains pending. See
[provider setup](provider-setup.md). No Gemini key was used against Google and the
installed preview was not upgraded. Vertex AI/OAuth and Interactions remain
unimplemented.
