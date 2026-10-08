// Native implementation evidence, not a supported-provider/model catalogue.
export const nativePartial = {
  anthropic: {
    sources: [
      ['Native/src/anthropic_request.cpp', 'serialize_anthropic_request'],
      ['Native/src/anthropic_stream.cpp', 'AnthropicStream::feed'],
      ['Native/src/anthropic_history.cpp', 'anthropic_history_content'],
      ['Native/src/chat_provider.cpp', 'ProviderWire::anthropic_messages'],
      ['Native/src/agent_runner.cpp', 'AgentRunner::execute'],
    ],
    evidenceFiles: ['doc/evidence/native-cleanup-hosted-provenance.json','doc/evidence/native-provider-profile-cli-hosted-provenance.json','doc/evidence/native-anthropic-agent-hosted-provenance.json','doc/evidence/native-anthropic-history-local-provenance.json','doc/evidence/native-anthropic-history-hosted-provenance.json'],
    scope: 'Native Claude Messages request/SSE/HTTP boundaries passed cleanup58 hosted text/tool fixtures. Exact ac69 hosted65 verifies paginated catalogue discovery, real CLI encrypted profile setup/selection and identity guards. Exact ae7c4ba hosted66 passed actual AgentRunner file effects, encrypted xlang3 SQLite reopen/tool-history replay, cancellation/recovery, SQL rollback and native/common-renderer usage metrics. Local67 and exact fceb50ba hosted67 additionally preserve ordered text/tool/thinking/redacted blocks and exact argument JSON, validate native receipt/DTO/finish provenance, enforce aggregate history/wire bounds and replay signature-bearing content through actual file execution and SQLite reopen without repeated effects. The hosted67 gate also passed unchanged frontend98/17, native/browser integration and18-asset VSIX verification. This provider evidence excludes newer direct-MCP graph69 source. Provider replies, keys and signatures are synthetic; opaque replay does not establish local cryptographic verification, thinking request controls, live account or complete model acceptance.',
    gaps: 'The 21 pinned Anthropic entries are not individually accepted. Live discovery/enrollment/inference, broader media/cache/thinking request controls, server tools/refusals, non-chat modes and complete per-model capability/error behavior remain incomplete or require their own evidence. Foreign signed/tool history conversion and actual rendered IDE acceptance remain pending.',
  },
  gemini: {
    sources: [
      ['Native/src/gemini_request.cpp', 'serialize_gemini_request'],
      ['Native/src/gemini_stream.cpp', 'GeminiStream::feed'],
      ['Native/src/gemini_provider.cpp', 'complete_gemini'],
      ['Native/src/gemini_history.cpp', 'gemini_model_request'],
      ['Native/src/agent_runner.cpp', 'AgentRunner::execute'],
      ['Native/src/provider_catalogue.cpp', 'ProviderCatalogueFormat::gemini'],
      ['Native/src/provider_profile_runtime.cpp', 'validate_execution'],
    ],
    evidenceFiles: [
      'doc/evidence/native-gemini-transport-hosted-provenance.json',
      'doc/evidence/native-gemini-agent-local-provenance.json',
      'doc/evidence/native-gemini-agent-hosted-provenance.json',
      'doc/evidence/native-gemini-history-hosted-provenance.json',
      'doc/evidence/native-gemini-enrollment-local-provenance.json',
      'doc/evidence/native-gemini-enrollment-hosted-provenance.json',
      'doc/evidence/native-provider-profile-cli-local-provenance.json',
      'doc/evidence/native-provider-profile-cli-hosted-provenance.json',
    ],
    scope: 'Native GenerateContent request/SSE/header-authenticated transport and signed DTO replay passed ddd1d3d hosted60; common gateway/history and escaped large results passed75f45f0 hosted61. Exact c9591fe hosted62 verifies actual AgentRunner file execution and SQLite signed replay. Exact2f5e0f0 hosted64 verifies native catalogue/profile enrollment, separate model tool policy, encrypted-key reuse, actual enrolled file execution/cancellation/reopen and thin Settings fixtures. Local65 and exact ac69 hosted65 add real CLI discovery/profile selection, safe diagnostics, explicit admission rebinding, signed agent history and failed-turn recovery. Provider sockets are labelled synthetic; no full provider-group or individual model acceptance is established.',
    gaps: 'The 84 pinned Gemini entries are not individually accepted. Live authenticated account discovery/enrollment/inference and actual Gemini IDE/graph acceptance remain pending. Vertex AI/OAuth, media, caching/thinking controls, Interactions, foreign signed/tool history conversion and complete per-model behavior remain unimplemented or unverified. Documentation-based tool declarations do not establish account access or per-model execution.',
  },
  openai: {
    sources: [
      ['Native/src/model_request.cpp', 'serialize_chat_request'],
      ['Native/src/responses_request.cpp', 'serialize_responses_request'],
      ['Native/src/responses_stream.cpp', 'ResponsesStream::feed'],
      ['Native/src/chat_provider.cpp', 'complete_model'],
      ['Native/src/provider_setup.cpp', 'ProviderRuntime::configuration'],
    ],
    evidenceFiles: [
      'doc/evidence/live-responses-provider-success.json',
      'doc/evidence/live-responses-mcp-diagnostic-read.json',
      'doc/evidence/native-provider-profile-cli-local-provenance.json',
      'doc/evidence/native-provider-profile-cli-hosted-provenance.json',
    ],
    scope: 'Native Chat Completions and Responses request/stream boundaries, encrypted backend enrollment and selected live Responses checks exist. Local65 and exact ac69 hosted65 add real CLI profile controls, OpenAI catalogue discovery/encrypted enrollment and shared identity guards using synthetic provider sockets. Evidence applies only to its exact revisions, configured model and recorded scenarios.',
    gaps: 'The 209 pinned OpenAI entries are not all tested or implemented. Non-chat modes, multimodal input/output, embeddings, batch, full reasoning/model capability controls and per-model acceptance remain incomplete. Other providers and OpenAI-compatible deployments require their own authentication, endpoint, wire and behavior validation.',
  },
};
