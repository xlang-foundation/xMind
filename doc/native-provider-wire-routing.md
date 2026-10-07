# Native provider wire enrollment

Native saved-provider enrollment now routes GPT-5.4, GPT-5.5, GPT-5.6, GPT-6 and
GPT-6.1 family selections to the approved Responses endpoint. Family matching is
explicit (exact identity or a hyphen suffix); it is not a general claim that all
discovered model IDs work. Other model selections retain the configured legacy
wire. The official default backend policy permits only OpenAI's fixed Chat
Completions and Responses URLs. A custom backend needs an explicitly supplied
trusted Responses endpoint; the client API still cannot set destinations.

The encrypted key is resolved inside the native backend under its current scope
and purpose, then stored as a new encrypted credential identity bound to the
candidate endpoint. Candidate execution is constructed before publishing the
active metadata. Failed configuration retains the previous service/reference;
unused encrypted candidates may remain. Revision and idle-run guards still
apply. There is no plaintext credential returned to clients or direct SQLite I/O.

Responses enrollment stores its wire together with endpoint/model/credential
reference. Reopen validates both wire and endpoint against backend policy before
resolving credentials. Existing five-field legacy records stay compatible. The
public configuration metadata now includes `wire`; it excludes credentials.
Returning to a legacy model securely rebinds the saved key to that approved route.
Conversation conversion remains forbidden by existing continuation checks.

Contracts were extended for saved-key rebinding, wire metadata, reopen, legacy
return and rejection of an unapproved persisted endpoint. Native compilation and
execution are pending: the guarded local build deferred for active sibling
benchmarks. Existing previews still use the tested diagnostics revision and the
live HTTP 400 has not been fixed or retried with this unverified routing source.
Actual Responses transport, reasoning controls in views, full model capability
coverage and live tool-loop acceptance remain required.

The policy follows the official [reasoning guide](https://developers.openai.com/api/docs/guides/reasoning)
and [Responses migration guide](https://developers.openai.com/api/docs/guides/migrate-to-responses),
opened on 2026-10-07. Responses supports reasoning with tools for these model
families; Chat Completions has model-specific restrictions. This routing change
keeps the selected model identity and does not set its reasoning effort to `none`.

The setup protocol fixture now also submits a real native run after routed
enrollment. Its independent Node peer accepts only the Responses request shape,
uses labelled synthetic SSE output/usage, and checks that the credential is only
in the authorization header. The C++ contract requires completion, persisted
response usage and retained conversation/run state after reopen. Exact peer
request counts prevent a skipped transport path from passing. JavaScript syntax
validation passed; compiled execution and live OpenAI acceptance remain pending.

The shared editor/browser adapters now accept only matching official OpenAI
endpoint/wire metadata: legacy Chat Completions (including older records without
a wire field) or explicit Responses. Mismatched pairs and arbitrary endpoints
are rejected before account discovery. Saved-key discovery and sidebar model
selection support Responses metadata; the footer displays the backend-reported
wire without inferring it from model names. All 69 extension and 11 browser
contracts pass locally, using labelled adapter/DOM fixtures. The updated browser
adapter also passed its actual C++/xlang3 contract against tested revision
`d0a70fe`, including native restart and retained cookie/history. This verifies
adapter compatibility with the existing native binary, not the newer enrollment
routing or a live Responses request. Native routing compilation and actual
provider acceptance are still pending.

The routing contract also injects a real xlang3/SQLite trigger failure while
publishing a return to Chat Completions. It requires the active metadata bytes,
Responses wire and revision to remain unchanged, then submits another actual
native Responses request through the preserved service/key. Only after that
request settles does it retry the explicit legacy selection. The independent
synthetic peer requires two Responses requests, so metadata checks alone cannot
satisfy this rollback case. Syntax checks passed; compiled execution is pending.

Switching a Responses conversation back to the Chat wire preserves its original
output items. The serializer now raises a typed incompatibility; the agent
records `incompatible_provider_history` before transport instead of the generic
agent error. The shared renderer explains that the user can start a conversation
or return to the previous wire, without inventing a reply or usage. The native
routing contract submits this incompatible continuation and requires preserved
history, a durable typed failure and no additional peer request. Native execution
of this addition remains pending: the local build guard observed active sibling
xlang3 benchmark processes and deferred compilation. All 70 extension and 11
browser adapter/DOM contracts pass locally. These checks do not establish live
OpenAI compatibility or completed native execution for this addition.
