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
