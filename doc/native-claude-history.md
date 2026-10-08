# Native Claude content history

The complete local **67-contract native gate passed in 117.65 seconds**, with
an exact manifest, zero failures/skips and no post-build exclusions. The new
receipt helper contract took **0.50 seconds** and the expanded actual Claude
AgentRunner contract **0.57 seconds**. Fresh browser/native integration passed
against the rebuilt server. Unchanged frontend sources match `ae7c4ba` and retain
its recorded **98 extension and 17 browser tests**; those suites were not rerun
for this native history change.
[Local source/runtime/binary/log provenance](evidence/native-anthropic-history-local-provenance.json).

The earlier [Claude agent/metrics acceptance](native-claude-agent.md) and
[hosted CLI65 evidence](evidence/native-provider-profile-cli-hosted-provenance.json)
have separate exact scopes. Prior source
`ae7c4ba07d59f57cbe393eff5a01b7d5186dfbc5` passed its exact hosted **66 native
contracts in 131.92 seconds**, **98 extension and 17 browser tests**,
native/browser integration and VSIX verification, with zero failures/skips. Its
Claude agent contract took **1.41 seconds**.
[Exact hosted66 evidence](evidence/native-anthropic-agent-hosted-provenance.json).
That gate excludes the current67 signed/interleaved receipts, history helper and
raw replay changes. Current hosted67 validation remains pending. No live
provider, model availability or installed-preview acceptance is claimed.

The native implementation retains the ordered Claude assistant content needed
for tool continuation through the existing C++ agent and embedded xlang3 SQLite
history. It adds response/history handling, while requests to enable thinking, set its
budget/display or map `reasoning_effort` remain unsupported.

## Primary protocol references

Claude's [streaming reference](https://platform.claude.com/docs/en/build-with-claude/streaming)
describes indexed content blocks, partial tool-input JSON, thinking deltas and a
signature delta before the thinking block closes. The
[thinking reference](https://platform.claude.com/docs/en/build-with-claude/thinking)
requires unchanged thinking/redacted blocks during tool continuation and treats
signatures and redacted data as opaque. The
[legacy extended-thinking page](https://platform.claude.com/docs/en/build-with-claude/extended-thinking)
now delegates these shared mechanics to the thinking overview. These sources
were checked on **2026-10-08**; the dated interpretations and native limits are in
[the history reference](evidence/anthropic-history-reference.json). That reference
records the source interpretation before execution; the local provenance above
records the later compiled acceptance.

## Ordered native receipt

New source stores a single `anthropic_content` item in the assistant's
`provider_items` array. Its fields are:

| Field | Meaning |
| --- | --- |
| `type` | Exact native tag `anthropic_content` |
| `content_json` | String containing the reconstructed ordered Claude content array |
| `finish_reason` | Native finish classification: `stop`, `tool_calls` or `length` |
| `provider_finish_reason` | Corresponding observed Claude stop reason |

Keeping the array inside a string lets AgentRunner's JSON persistence preserve
accepted tool-input number tokens and escaped keys without converting and
re-dumping them. This retains block order and boundaries, including text/tool
interleaving. It does not preserve raw HTTP/SSE framing. Text blocks contribute
to ordinary assistant content; thinking, signatures and redacted data remain
provider continuation material and produce no ordinary `model.text` output.

The [history helper](../Native/src/anthropic_history.cpp) checks the receipt
against its assistant DTO before outgoing transport: concatenated text must equal
visible content, and ordered tool IDs, names and raw argument strings must match
the native call list exactly. Duplicate/unknown fields, duplicate call IDs,
foreign receipt tags, empty content and inconsistent terminal classifications
fail closed. Common native request validation still requires matching results
for every pending call. The
[Claude serializer](../Native/src/anthropic_request.cpp) inserts validated ordered
content arrays without re-dumping tool inputs; it retains legacy text/tool DTO
support without fabricating private blocks or signatures.

The [stream decoder](../Native/src/anthropic_stream.cpp) requires a completed
thinking block to carry a nonempty signature. It retains signature deltas only
as an opaque string and rejects further thinking text after signature emission.
Redacted blocks preserve opaque `data`. These checks validate structure and
continuation consistency; xMind neither decrypts nor cryptographically verifies
signatures/data. They do not prove provider origin, model/account compatibility
or that a live provider will accept the replay. Length-truncated tool blocks
cannot become executable calls or a matching replay receipt.

## Native bounds

These limits reflect the inspected native implementation, not Claude API limits.
All sizes are bytes; MiB means 1,048,576 bytes.

| Boundary | Limit |
| --- | --- |
| Content array source and serialized receipt envelope | 8 MiB each; envelope escaping/overhead counts |
| Ordered content blocks | 64 |
| Aggregate visible assistant text | 4 MiB |
| Thinking text or redacted data per block | 4 MiB |
| Signature per thinking block | 65,536; nonempty and no NUL |
| Redacted data | Nonempty valid UTF-8 with no NUL |
| Tool input JSON | 1 MiB; object, no duplicate keys, maximum parser depth 16 |
| Content JSON / receipt-envelope parser depth | 32 / 4 |
| Tool ID / name | 256 / 64; names use ASCII letters, digits, `_` and `-` |
| Aggregate incoming `provider_items_json` in one request | 8 MiB, checked before copying |
| Request messages / offered tools | 4096 / 64, checked before receipt allocation |
| Outgoing serialized Messages request | 8 MiB |
| SSE total bytes / event data / line | 64 MiB / 4 MiB / 1 MiB |
| SSE event name / event JSON parser depth | 256 / 64 |

The serialized envelope can hit its limit before its embedded content reaches
8 MiB. A receipt does not relax the existing message/tool counts or request
capability rules. Empty thinking text with a supplied nonempty signature remains
distinct from an unsigned thinking block.
Same-role merging and final-body limits apply before oversized concatenation.
An incoming empty natural-stop receipt can be retained; outgoing zero-block
assistant replay is explicitly incompatible and is not silently skipped.

## Verified local acceptance

The [compiled helper contract](../Native/tests/anthropic_history_contract.cpp)
passed exact numeric tokens and escaped argument keys across agent-shaped receipt
JSON round trips and outgoing serialization, ordered DTO matching, opaque
signed/redacted blocks and malformed/foreign/over-limit receipts. Aggregate
receipt, message/tool count, same-role merge and serialized-body bounds passed.
The expanded [actual Claude agent fixture](../Native/tests/anthropic_agent_contract.cpp)
performed two file reads through signed/interleaved blocks, preserved escaped
argument keys and replayed the ordered conversation after xlang3 SQLite
close/reopen without repeating effects. Unsigned blocks failed before tool
execution. A signature-only final thought retained its supplied signature
without entering ordinary answer text. Existing protocol rejection,
cancellation/recovery and SQL-rollback cases also passed.

Provider replies and signature/data values in these tests are labelled synthetic;
they cannot establish cryptographic authenticity or live inference.

Exact hosted evidence and any later UI/live scope must be recorded separately.
Thinking controls, broader Messages variants,
cross-wire receipt conversion, model/account binding, all 21 pinned Anthropic
models and full provider/coding/protocol parity remain incomplete.
