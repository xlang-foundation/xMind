# Native Claude agent acceptance

This records the earlier `ae7c4ba` text/tool/metrics checkpoint. The subsequent
[Claude history checkpoint](native-claude-history.md) adds ordered raw receipts
and signature-bearing thinking replay with its own exact validation scope.

The Claude checkpoint passed its complete local **66-contract native gate in
114.49 seconds**, with an exact manifest and no post-build exclusions. The actual
Claude AgentRunner contract passed in **0.56 seconds** and its gateway/transport
adapter contract in **0.23 seconds**. Frontend suites passed **98 extension and
17 browser tests**, with zero failures/skips across all three suites. Fresh
browser/native integration also passed against the rebuilt server and matching
assets using disposable processes and storage.
[Local source, binary, runtime and log provenance](evidence/native-anthropic-agent-local-provenance.json).

This verifies actual C++ AgentRunner execution, filesystem reads and embedded
xlang3 SQLite history with an independent synthetic Messages provider. The new
metrics fixtures exercise the actual shared renderer DOM. These scopes do not
establish live Claude inference or actual rendered Claude IDE acceptance. Exact
hosted validation of the 66-contract source remains pending; installed previews
are unchanged.

Claude uses the existing `anthropic.messages` profile route, native transport and
backend-owned reference to an encrypted credential. The agent and repository own
tool execution, run state and history. The browser and VS Code views consume the same
command/event and response metadata; they do not execute tools or compute usage.

## Verified native execution cases

The compiled [agent contract](../Native/tests/anthropic_agent_contract.cpp) and
[independent socket peer](../Native/tests/anthropic_agent_peer.mjs) passed these
boundaries:

- Two `read_file` calls read real owned fixture files. The provider peer checks
  returned bytes and matching `tool_use.id`/`tool_result.tool_use_id` before
  supplying the final answer. Native history retained the prompt, assistant
  calls, both tool results and final assistant response in order.
- Closing and reopening xlang3 SQLite preserved exact history bytes, the owned
  credential reference, encrypted key material and completed run ownership. A new
  actual run replayed the stored conversation without executing earlier tools
  again.
- Malformed, unoffered, incomplete, post-terminal-error and length-truncated
  calls failed without native tool execution or a successful assistant row.
  Observed partial stream events are not proof of a completed response.
- Cancelling an actual held provider socket persisted cancelled ownership and
  retained only the prompt. A later real run in that session recovered without
  replaying the discarded partial assistant text.
- An injected SQL failure on the second tool-result row rolled back assistant
  and tool history together. Both preceding file reads remain actual recorded
  effects; the failed conversation batch does not imply that those reads were
  undone or that the conversation completed.

These are bounded synthetic-provider contracts, not external account or model
availability tests. Fixture credentials and provider replies are labelled test
data. The contract checks that keys and private provider error bodies do not
enter persisted conversations or events.

## Supplied usage and view labels

Claude reports uncached input separately from cache writes and reads. See the
official [prompt-caching usage reference](https://platform.claude.com/docs/en/build-with-claude/prompt-caching).
Stream `message_delta` usage counters are cumulative, so the native decoder
retains the latest supplied counts rather than adding streamed updates.
[Official streaming reference](https://platform.claude.com/docs/en/build-with-claude/streaming).
The dated interpretations are recorded in
[the usage reference](evidence/anthropic-usage-reference.json).

The raw `AnthropicStream` counters remain unchanged. The shared native gateway
uses one normalization path for `model.usage` events and `ModelCompletion`, which
AgentRunner persists:

| Supplied Claude field | Shared response field | View label |
| --- | --- | --- |
| `input_tokens` | Raw field plus `prompt_tokens` and `input_tokens_scope: "uncached"` | Input (uncached) |
| `output_tokens` | Raw field plus `completion_tokens` | Output |
| `cache_creation_input_tokens` | Preserved raw field | Cache write |
| `cache_read_input_tokens` | Raw field plus `prompt_tokens_details.cached_tokens` | Cached |

Mappings are added only when their source field is supplied. Zero remains zero;
missing counters remain absent. Neither backend nor view sums cache counters
into input, synthesizes `total_tokens`, or estimates reasoning tokens or cost.
Unavailable totals display a dash. This preserves the meaning of Claude's
uncached input rather than presenting it as all processed input. Reading supplied
cache counters does not add cache-control configuration or prove caching works
for a live account.

The [adapter contract](../Native/tests/anthropic_provider_contract.cpp) passed
event/completion consistency, nonzero and zero cache counters, absent cache fields
and malformed-usage cases in the same native gate. Passing
[renderer fixtures](../extensions/vscode/tests/renderer.test.js)
cover live events, persisted responses, supplied zeros, unavailable fields and
preservation of other providers' labels. Native response timings remain measured
by the backend.

## Remaining acceptance

At this earlier checkpoint, thinking and redacted-thinking blocks, signed Claude continuation, broader media,
server tools, caching controls and complete per-model behavior remain incomplete.
Unsupported blocks fail explicitly. This checkpoint does not add cross-wire
conversion of foreign provider receipts, establish live Claude discovery or
inference, accept the pinned Anthropic model inventory, or upgrade the installed
preview. The 21 pinned Anthropic models and full provider/coding/protocol parity
remain incomplete. Exact hosted validation of this newer source is still pending
and must retain its own source and runtime scope.
