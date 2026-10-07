# Model and provider support

The user's subsequent requirement is native C++ platform implementation. LiteLLM is a reference only: implement model/provider adapters and routing in C++, rather than use its Python SDK as AgentFlow's core. The SDK dependency notes below are historical audit evidence, not an approved integration plan.

The user requested LiteLLM as the reference for broad frontier-model support. This extends the existing OpenCode parity requirement. Model access belongs in the shared backend so single agents, graph nodes, CLI and editor clients use the same provider configuration and capability checks.

## Pinned reference

LiteLLM SDK version 1.106.0, repository commit `e98bbc2f8e035a382044524c73341d8b5800d492`, inspected October 6, 2026. Source: https://github.com/BerriAI/litellm/tree/e98bbc2f8e035a382044524c73341d8b5800d492 . Provider documentation: https://docs.litellm.ai/docs/providers .

Pinned `pyproject.toml`, model catalogue and license are retained in `.agentflow/reference/litellm` for source inspection. `node Tools/audit-litellm.mjs` generates `LITELLM_PROVIDER_INVENTORY.json`, grouping model entries by upstream provider, mode and declared capability. It excludes the sample specification. Model aliases, deployment regions and non-chat entries count separately. Metadata does not prove model availability or AgentFlow interoperability. Pricing and capability updates need an explicit refreshed baseline; do not silently fetch executable code or treat old prices as current.

## Integration requirements

- Support provider-qualified model IDs, configurable endpoints and provider-specific authentication. Credentials remain in the backend and must be redacted from logs and client responses.
- Normalize streaming text, tool-call fragments, reasoning events where available, finish reasons, usage and provider errors without losing provider-specific data needed for continuation.
- Maintain per-model capability information for tools, parallel calls, reasoning controls, structured output, images, audio and other input types. Unknown capability stays unknown. Reject unsupported requests explicitly instead of silently dropping requested features.
- Provide a model discovery/configuration API shared by CLI and VS Code, including user-defined deployments and local models absent from the catalogue.
- Validate native C++ provider adapters against the LiteLLM coverage reference, including cancellation, timeouts, bounded retries and fallback behavior. Avoid retrying an already partially streamed response as if it were a new response.
- Cover frontier model families from OpenAI, Anthropic, Google, xAI and other catalogue providers, as well as cloud-hosted and local deployments. Coverage must be evaluated by actual provider/model contracts, not a hard-coded promise that every catalogue entry works.

## SDK compatibility audit

The pinned base SDK declares `fastuuid`, `tiktoken`, `tokenizers`, `aiohttp`, `pyyaml`, `pydantic`, OpenAI SDK and other dependencies. Python 3.14 is inside its declared supported range. This declaration does not establish xlang3 compatibility. xlang3 has existing YAML and pydantic-core bindings; the other native dependency surfaces need installation/import and behavioral probes before choosing direct SDK integration.

Execute all Python probes and pip installations with xlang3. Install pure-Python package sources only; never install or load CPython native extension binaries. Discuss each confirmed missing native API with the user before adding native bindings or selecting a workaround. A separate CPython LiteLLM service has not been authorized and is not the default integration plan. No dependency changes were made by this audit.

## Current evidence

The native `ChatCompletionStream` decoder passed its synthetic SSE protocol contract, including arbitrary fragmentation, parallel function-call arguments, usage and incomplete-stream rejection. See [native-model-stream.md](../doc/native-model-stream.md). It is connected to the Windows native HTTPS transport, which passed independent socket, cancellation and timeout contracts; see [native-provider-transport.md](../doc/native-provider-transport.md). Neither component establishes live model support or an agent engine. No provider credentials were configured in the current development environment when this check was made; live validation remains pending the selected endpoint.

`agentflow/providers.py` currently implements only OpenAI-compatible Chat Completions streaming. Existing test doubles establish the initial streaming contract; no live frontier provider or LiteLLM SDK integration is verified. Broad provider support, reasoning/multimodal events, model discovery, routing and SDK compatibility remain required work.
