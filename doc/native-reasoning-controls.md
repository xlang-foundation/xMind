# Native reasoning controls

`ChatProviderConfig` now has optional typed `ReasoningEffort` and an explicit
reasoning capability. Effort values are `none`, `minimal`, `low`, `medium`,
`high`, `xhigh` and `max`. Unsupported/unknown reasoning capability rejects an
explicit setting before networking; invalid enum values are rejected. This
generic serializer boundary does not infer support from a model name. Native
model/wire policy must select supported values for each model.

Chat Completions serializes `reasoning_effort`; Responses serializes
`reasoning.effort` and excludes the Chat-only field. Unset effort stays omitted.
Tests cover explicit values, invalid/unsupported settings, omission and wire
shape. Existing continuation validation and encrypted Responses items remain
unchanged.

This is source-prepared infrastructure, not the live HTTP 400 fix. Provider
enrollment, persisted wire/effort metadata, credential endpoint binding and view
controls still need integration. The guarded local native build deferred for
live sibling benchmarks; compiled contract execution is pending in the hosted
pipeline. No running preview has received these unverified changes.

The protocol references are the official [Chat Completions API](https://developers.openai.com/api/reference/resources/chat/subresources/completions/methods/create)
and [reasoning guide](https://developers.openai.com/api/docs/guides/reasoning),
inspected on 2026-10-07. Values and tool support remain model-dependent. The
recorded live failure and required routing work are in
[the compatibility investigation](live-provider-compatibility.md).
