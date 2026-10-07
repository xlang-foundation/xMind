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
