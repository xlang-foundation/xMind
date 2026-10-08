// Native implementation evidence, not a supported-provider/model catalogue.
export const nativePartial = {
  anthropic: {
    sources: [
      ['Native/src/anthropic_request.cpp', 'serialize_anthropic_request'],
      ['Native/src/anthropic_stream.cpp', 'AnthropicStream::feed'],
      ['Native/src/chat_provider.cpp', 'ProviderWire::anthropic_messages'],
    ],
    evidenceFiles: ['doc/evidence/native-cleanup-hosted-provenance.json'],
    scope: 'Native Claude Messages request, SSE and header-authenticated HTTP boundaries passed the exact cleanup58 hosted gate, including synthetic socket tool/signature continuation. This is component evidence; default provider-profile routes exist but no live Anthropic inference or complete agent/model acceptance is established.',
    gaps: 'The 21 pinned Anthropic entries are not individually accepted. Live model discovery/enrollment/inference, actual Claude agent effects, broader media/cache/thinking controls, non-chat modes and complete per-model capability/error behavior remain incomplete or require their own evidence.',
  },
  gemini: {
    sources: [
      ['Native/src/gemini_request.cpp', 'serialize_gemini_request'],
      ['Native/src/gemini_stream.cpp', 'GeminiStream::feed'],
      ['Native/src/gemini_provider.cpp', 'complete_gemini'],
      ['Native/src/gemini_history.cpp', 'gemini_model_request'],
      ['Native/src/agent_runner.cpp', 'AgentRunner::execute'],
    ],
    evidenceFiles: [
      'doc/evidence/native-gemini-transport-hosted-provenance.json',
      'doc/evidence/native-gemini-agent-local-provenance.json',
    ],
    scope: 'Native GenerateContent request/SSE/header-authenticated transport and signed DTO replay passed the exact ddd1d3d hosted60 gate with synthetic peers. The newer working-tree local62 gate verifies common gateway/history, exact callbacks, escaped large results and actual AgentRunner two-file execution with xlang3 SQLite reopen/signed replay, using synthetic provider replies. No full provider-group or individual model acceptance is established.',
    gaps: 'The 84 pinned Gemini entries are not individually accepted. Default Settings enrollment/catalogue discovery, live inference and actual Gemini IDE/graph acceptance remain pending. Vertex AI/OAuth, media, caching/thinking controls, Interactions and complete per-model behavior remain unimplemented or unverified. The newer exact checkpoint still requires hosted validation.',
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
    ],
    scope: 'Native Chat Completions and Responses request/stream boundaries, encrypted backend enrollment and selected live Responses checks exist. Evidence applies only to its exact revisions, configured model and recorded scenarios.',
    gaps: 'The 209 pinned OpenAI entries are not all tested or implemented. Non-chat modes, multimodal input/output, embeddings, batch, full reasoning/model capability controls and per-model acceptance remain incomplete. Other providers and OpenAI-compatible deployments require their own authentication, endpoint, wire and behavior validation.',
  },
};
