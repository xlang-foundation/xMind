// Native implementation evidence, not a supported-provider/model catalogue.
export const nativePartial = {
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
