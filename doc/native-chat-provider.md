# Native Chat Completions provider adapter

The C++ adapter now connects typed request serialization, the native Windows transport and incremental model decoding. The endpoint and model/deployment ID are explicit configuration. Credentials remain separate backend-owned secret buffers and are not serialized into the model body. The adapter does not invoke tools or manufacture model output.

The wire schema follows the official [Chat Completions request reference](https://developers.openai.com/api/reference/resources/chat/subresources/completions/methods/create), checked October 6, 2026. This establishes the selected wire protocol, not compatibility with every provider/model that exposes a similar endpoint.

`ModelRequest` carries typed role/content messages, assistant function calls, matching tool results, function definitions, optional streamed usage and an output token limit. Feature capabilities are `unknown`, `unsupported` or `supported` and default to unknown. Tools, usage and output-limit requests require explicit supported declarations; the adapter never silently removes a requested option. A configuration declaration is not evidence of live capability verification.

The serializer preserves parallel tool-call identities and requires every pending call to have one matching result before continuation. It rejects missing, duplicate or mismatched results, spoofed tool-call roles, invalid argument JSON, duplicate function definitions and invalid UTF-8. JSON schemas are transported intact and checked only as bounded JSON objects here; full schema validation and tool authorization belong to the tool layer. It checks aggregate source sizes before allocating the serialized request and enforces the transport's 8 MiB final body bound.

After network and decoder completion, the adapter rejects returned tool names outside the offered definitions before publishing `model.done`. A provider turn completing is distinct from agent-run completion. Length/refusal/filter outcomes remain explicit; tool effects require independent permission and schema checks. This text/function-call adapter does not claim reasoning/multimodal continuation, provider-specific extension replay or all frontier-model features.

## Verified scope

Ten native contracts passed in Release. The new request contract checks capability gates, exact caller content and schema preservation, parallel continuation identity and invalid inputs. The independent Node wire peer checks the compiled adapter's actual POST body/authentication, returned tool-call reconstruction, incomplete-stream failure and unoffered-tool rejection without a completion event. An unknown capability is rejected before any request reaches that peer.

Fixtures are explicitly synthetic. No inference service or real model was called, and no product run route was enabled. Evidence: [native-chat-provider-ctest.log](evidence/native-chat-provider-ctest.log) and final request/adapter checks after the aggregate-size guard in [native-chat-provider-final.log](evidence/native-chat-provider-final.log).

The [native agent loop](native-agent-loop.md) now invokes this adapter and resolves encrypted credential references internally. Live provider validation still requires the chosen endpoint/model and privately configured credentials. Backend configuration/discovery, product HTTP/CLI scheduling, complete coding tools/policies, other protocol families, routing/retries and provider coverage against LiteLLM remain required. The project's full goal remains unchanged.

The regenerated [pinned LiteLLM coverage inventory](../doc/LITELLM_PROVIDER_INVENTORY.json)
now distinguishes declared upstream capabilities from native implementation
evidence. Its 4,483 entries span 136 provider groups. OpenAI is one partial native
candidate with checked C++ source anchors/hashes and selected recorded live
Responses evidence; the other 135 groups are unmapped. Zero complete provider
groups have verified parity. The 209 pinned OpenAI entries include modes and
models beyond those live checks; their catalogue presence is not tested support.
The audit rejects catalogue-byte drift from the pinned SHA-256 and hashes native
source with normalized line endings. `node Tools/audit-litellm.mjs` checks and
regenerates the inventory without importing or executing LiteLLM. Native provider
implementation and per-model acceptance remain required for the requested broad
coverage; xlang3 owns embedded scripts and database I/O, while C++ owns provider
networking, wire adapters and model orchestration.

The native model library now has source for a separate Claude Messages request
component, `serialize_anthropic_request`. It reuses our common native validation
and writes Claude text/system, tool-use and tool-result blocks with original
content and call ownership. Consecutive same-role blocks retain their order;
tool schemas and arguments reject duplicate keys and excessive nesting. An
explicit supported output limit is required. Unsupported reasoning controls,
developer-role authority mapping, refusal metadata, late system instructions and
foreign provider continuation data are rejected rather than silently discarded.

Its new C++ contract has exact wire-shape cases and rejection cases for pending
or foreign call results, ambiguous JSON and incompatible history. Compilation
was initially deferred by sibling benchmark process 8576. After the guard
cleared, all 53 native contracts passed locally, including the new request
component: [component log](evidence/native-anthropic-request-local.log),
[source/runtime provenance](evidence/native-anthropic-request-local-provenance.json).
This is local component validation; exact hosted validation remains pending.
Existing product routing
still has only Chat Completions and Responses; Claude transport routing, version
headers, native backend enrollment/capability policy and actual provider
acceptance are still required. The LiteLLM inventory therefore does not count
this request component as an implemented Claude provider.

The request structure follows the official [Claude Messages reference](https://platform.claude.com/docs/en/api/messages/create),
checked on 2026-10-07. This component implements no SDK or LiteLLM execution.

The separate native `AnthropicStream` decoder now reconstructs Claude text and
tool-use blocks from incremental SSE bytes, preserving call identities and actual
reported input/output/cache token counters. It validates block ownership,
duplicate JSON keys, cumulative usage and terminal lifecycle. Truncated messages
never publish `model.done`; length-limited tool blocks never become executable
calls. Provider error payloads are not exposed in protocol diagnostics. Thinking,
redacted thinking, server tools and other unsupported content/stop variants fail
explicitly; this is not complete Claude feature parity.

All 54 native contracts passed locally in 102.90 seconds. The new decoder contract
uses synthetic text/tool events, fragmented Unicode, every truncated prefix,
multiple calls, usage-only updates and invalid lifecycle/JSON cases. Evidence:
[decoder log](evidence/native-anthropic-stream-local.log) and
[source/runtime provenance](evidence/native-anthropic-stream-local-provenance.json).
The decoder is not yet connected to product provider routing or the installed
preview. Native version headers, enrollment/capability policy and live Claude
acceptance remain required before claiming an available Claude provider.

The next source checkpoint connects `ProviderWire::anthropic_messages` to
`complete_model`: it serializes our native request, sends a backend-owned API key
through the native transport and feeds actual response bytes to `AnthropicStream`.
The transport's closed protocol enum adds only the fixed
`anthropic-version: 2023-06-01` header for Claude. Generic/OpenAI requests retain
their existing headers; unknown protocol enum values are rejected. The header
policy follows the official [Claude authentication reference](https://platform.claude.com/docs/en/manage-claude/authentication),
checked on 2026-10-07; workspace selection and federation credentials remain
outside this initial workspace-key adapter.

An independent Node HTTP peer and native adapter contract are added for exact
request/authentication/version headers, text/tool-result continuation, unoffered
tools, truncated streams, post-terminal errors, HTTP failures and forbidden
redirects. The generic transport contract also checks that Claude headers are
absent and unknown protocol values never reach the peer. These new adapter
contracts initially waited for the local build guard to clear sibling benchmark
processes 26056, 24540 and 26516. The subsequent build passed all **55 native
contracts** in **92.54 seconds**, including the independent Claude adapter peer:
[adapter log](evidence/native-anthropic-provider-local.log),
[source/runtime provenance](evidence/native-anthropic-provider-local-provenance.json).
This validates local request/HTTP/stream interoperability against synthetic wire
events, not real Claude inference or product enrollment.

The earlier hosted request checkpoint built successfully but its exact test-set
guard rejected 53 registered contracts against an outdated 52-entry list, before
CTest executed. [Failure provenance](evidence/native-anthropic-initial-ci-registration-failure.json).
The guard now explicitly includes all three new Claude contracts and locally
matches the full 55-entry configured set; its exact-set comparison is retained.
Corrected hosted validation, product enrollment and live Claude inference remain
pending. The running preview retains its previously verified bundle.

Corrected gate `df423591eb89540041f030228db42ec4dccbfcf5` subsequently passed
all **55 native, 73 extension and 14 browser contracts**, with no failures/skips:
[hosted CTest](evidence/native-anthropic-adapter-hosted-ctest.log),
[exact source/runtime provenance](evidence/native-anthropic-adapter-hosted-provenance.json).
The native suite includes the Claude request, stream and actual HTTP adapter
contracts using the independent synthetic peer. Packaging and runtime publication
also succeeded. This gate does not include the newer provider-profile registry;
its separate 56-contract gate is running. Product Claude Settings enrollment,
live inference and full provider feature parity remain incomplete. No preview
runtime was replaced for this component validation.
