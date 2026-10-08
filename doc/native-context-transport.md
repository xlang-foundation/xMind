# Native JSON POST transport

At immutable source `859aca7743e561e46fd574c82dff65df333a7bf4`, the native
provider transport supports bounded, non-streaming JSON POST alongside JSON
GET and streaming POST. The exact hosted checkpoint passed **78 native
contracts in 246.07 seconds**, **119 extension contracts** and **26 browser
contracts**; all **16 workflow steps** succeeded. Its transport contract passed
in **9.80 seconds**. [Hosted source and artifact evidence](evidence/native-context-transport-hosted-859-provenance.json).

The earlier complete local **78-contract gate passed in 193.91 seconds**, with
all **427 frozen inputs** and the accepted xlang3 runtime files verified
unchanged. Its transport contract passed in **8.80 seconds**. The earlier
strict compiler rejection and subsequent successful gate remain in the
[local evidence](evidence/native-context-transport-local-provenance.json).

This Git859 checkpoint supplies the HTTP primitive for token-counting and
compaction adapters; it does not implement or validate those adapters, durable
compacted contexts or client controls. The later compaction work has a separate
validation gate. See the [full native compaction design](native-context-compaction-design.md).

## Native API

`post_json(request, credential, max_response_bytes, cancel)` is declared in
`Native/include/agentflow/http_stream_transport.hpp` and implemented through
the existing native WinHTTP transport. The caller supplies serialized request
bytes, a backend-selected endpoint and an explicit response cap from **1 byte
through 8 MiB**. Empty bodies, request bodies above 8 MiB and invalid response
caps reject before dispatch.

Successful replies require the JSON media type and return their original
bytes, including UTF-8 and opaque provider fields. The native provider adapter
must validate the resulting protocol JSON. This transport does not manufacture
model messages, usage, checkpoints or tool calls from a JSON reply.

The existing credential placement, TLS certificate verification, loopback HTTP
restriction, cancellation, deadline and idle timeout rules apply. Redirects
are not followed, ambient credentials are not sent, and HTTP errors retain
only bounded allowlisted diagnostics. There is no automatic retry. Existing
JSON GET remains capped at 1 MiB; streaming POST retains its 64 MiB cap.

## Verification limits

The independent synthetic HTTP/TLS peer asserts exactly **15 native JSON POST
requests**. It checks the three credential placements, the selected Claude
version header, exact numeric/escaped-key/UTF-8 request bytes, split UTF-8 reply
bytes, exact response limits, replies larger than the discovery cap, sanitized
errors, redirects, cancellation, deadlines, idle timeout and reuse after failure.
Invalid configuration and untrusted TLS cases do not reach its HTTP handler.

That request count is a source-bound assertion reached by the passing contract;
it is not a separately retained raw request audit. The hosted
[CTest log](evidence/native-context-transport-hosted-859-ctest.log) contains all
78 successful contracts, and the original
[per-test transcript](evidence/native-context-transport-hosted-859-last-test.log)
contains all 78 matching test blocks. Fixture counts and opaque strings are
synthetic protocol data, not live provider usage or compaction acceptance.

The byte-preserved [GitHub job log](evidence/native-context-transport-hosted-859-ci-job.log)
retains its UTF-8 BOM and the original extension TAP timing. Separate original
[browser controller](evidence/native-context-transport-hosted-859-browser.log),
[model-free browser/native integration](evidence/native-context-transport-hosted-859-browser-native.log)
and [VSIX verification](evidence/native-context-transport-hosted-859-vsix.log)
logs retain their distinct scopes. Verification binds 423 native/build-source
inputs and 462 exact Git files, with separate raw and CRLF-normalized maps for
32 UI files. The downloaded bundle has 29 runtime files, 12 browser assets and
30 VSIX files; every extracted payload was checked against its original ZIP
bytes. These checks do not install or execute the downloaded bundle.

The earlier [hosted MCP checkpoint](evidence/native-dynamic-mcp-hosted-2cd-provenance.json)
passed 78 native, 119 extension and 26 browser tests at exact source `2cd392f`.
It precedes this transport change. Neither transport evidence publication nor
its hosted gate upgrades the installed preview or validates a live provider,
installed IDE, token counting or compaction.
