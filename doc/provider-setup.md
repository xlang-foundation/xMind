# Native provider setup

## Native Claude ordered history checkpoint

The latest local **67-contract native gate passed in 117.65 seconds**, with an
exact manifest, zero failures/skips and no post-build exclusions. It verifies
ordered `anthropic_content` receipts with string-valued `content_json`, exact
tool-argument tokens and escaped keys, opaque signed/redacted blocks and strict
receipt/DTO matching. The expanded actual AgentRunner fixture reads two files,
replays signed/interleaved history after xlang3 SQLite reopen without repeating
tools, and rejects unsigned blocks before effects. Signatures/data are retained
as opaque provider material; no local cryptographic verification is performed.

Fresh browser/native integration passed. Frontend sources match `ae7c4ba` and
retain its recorded **98 extension and 17 browser tests**, without rerunning
those suites for this native change. Current hosted67 validation remains pending.
Thinking request controls, `reasoning_effort` mapping, live models, rendered IDE
acceptance and cross-wire signed conversion remain incomplete; installed
previews are unchanged.
[Local67 evidence](evidence/native-anthropic-history-local-provenance.json),
[Claude history scope](native-claude-history.md).

## Native Claude agent and metrics checkpoint

Claude uses the existing `anthropic.messages` profile route and backend-owned
credential storage. New native gateway source preserves supplied input/output
and cache counters consistently in usage events and saved responses. Input is
labelled **Input (uncached)**; cache writes and reads remain separate, supplied
zeros survive, and missing totals are not calculated. The semantics follow the
dated [official usage reference](evidence/anthropic-usage-reference.json).

The preceding local **66-contract native gate passed in 114.49 seconds**, with
an exact manifest, zero failures/skips and no post-build exclusions. It verifies
actual two-file AgentRunner execution, xlang3
SQLite close/reopen and tool-history replay, held-stream cancellation/recovery
and second-tool-row SQL rollback. Frontend suites passed **98 extension and 17
browser tests**, with zero failures/skips, including actual shared-renderer DOM
fixtures. Fresh browser/native integration passed against the rebuilt server;
installed previews are unchanged. Exact source
`ae7c4ba07d59f57cbe393eff5a01b7d5186dfbc5` also passed its hosted **66 native
contracts in 131.92 seconds**, **98 extension and 17 browser tests**,
native/browser integration and VSIX verification, with zero failures/skips. Its
Claude agent contract took **1.41 seconds**.
[Exact hosted66 evidence](evidence/native-anthropic-agent-hosted-provenance.json).
That gate excludes the newer ordered thinking/signature history source above.
Thinking controls, live Claude inference, rendered Claude IDE acceptance and the
21 pinned models remain incomplete.
[Local evidence](evidence/native-anthropic-agent-local-provenance.json),
[Claude acceptance scope](native-claude-agent.md).

## Native Gemini discovery and enrollment

The backend advertises `gemini.generate-content` among its provider routes,
using Google's versioned GenerateContent base and its separate models-list
endpoint. Authenticated `/v1/provider/profiles` metadata advertises the actual
route and discovery capability. The existing Settings dialog obtains the key;
returned models stay in the footer chooser. Views submit a route/profile/model
identity and expected registry revision, and cannot replace the endpoint,
authentication header or native capability policy.

Catalogue discovery sends a read-only native GET with `x-goog-api-key`, fixed
`pageSize=1000` and encoded opaque `pageToken` values. It follows at most eight
pages and counts at most 4096 raw entries, with bounded JSON, duplicate-key,
resource, cursor, credential-reflection, deadline and redirect checks. Only full
`models/<id>` resources advertising `generateContent` are returned; embeddings
and undeclared generation methods do not become agent models. Discovery retains
the returned resource name rather than deriving an identity from display labels
or `baseModelId`. It does not publish a profile or persist a draft key.

Generation-method eligibility is separate from function-call capability. Native
backend `model_tools` policy declares exact model capabilities; the default
Gemini policy uses the dated [official-reference record](evidence/google-gemini-tool-policy-reference.json),
with no wildcard inference for new names or aliases. A workspace agent sees only
models with declared tool support and rejects unknown-tool enrollment before
candidate credential persistence. A text-only agent can use an eligible model
whose tool capability is unknown, sending no tool declarations. Canonical
`models/<id>` and valid bare identities resolve the same native capability policy.

Enrollment saves the key through encrypted backend credentials and xlang3
SQLite, then publishes the validated execution service with a registry revision
check. Selection resolves the saved credential and applies the same native model
policy before publication. Saved-key discovery resolves that profile's owned
credential privately; public metadata excludes keys, credential references and
endpoint URLs. Stale
discovery, selection or admission fails without task replay or partial publication.
Discovered model resources containing the provider key are rejected. The generic
profile runtime applies the same public-identity guard across all routes during
draft/save/import/reopen validation, including saved-key updates, and before
account discovery. Key-reflected profile/model identities fail before public
publication or candidate credential persistence; a reflected discovery profile
ID sends no provider request.
Reopen validates inactive profiles' resource grammar and public identities;
workspace tool capability is required when an active profile is prepared or an
inactive profile is selected. A valid inactive text-only model can remain saved
while a different coding profile runs.

The native enrollment acceptance sources exercise authenticated API boundaries, actual
SQLite publication rollback, key reuse/reopen, native file-tool continuation,
cancellation ownership and discovery/selection races with independent synthetic
catalogue/model peers. The enrollment milestone passed **64 native contracts locally in
120.98 seconds**, with an exact expected manifest and no post-build exclusions.
The same milestone passed **94 extension and 17 browser tests**, with zero
failures/skips across all three suites.
[Enrollment local evidence](evidence/native-gemini-enrollment-local-provenance.json).
The committed enrollment source `2f5e0f05e9bfadcf86b5508863da0ec5a9e78cfe`
passed its exact hosted gate: **64 native contracts in 158.09 seconds**, **94
extension and 17 browser tests**, actual browser/native integration and VSIX
verification, with zero failures/skips.
[Hosted evidence](evidence/native-gemini-enrollment-hosted-provenance.json),
[hosted run](https://github.com/xlang-foundation/xMind/actions/runs/37743203538).
The preceding generic profile CLI and all-route identity guards passed **65 native
contracts locally in 113.16 seconds**, with the exact manifest, zero
failures/skips and no post-build exclusions. Actual browser/native integration
passed again against the fresh native server. At that CLI checkpoint, frontend
sources were unchanged from `2f5e0f0`; the prior hosted frontend results were not
repeated or counted as new tests.
[CLI local evidence](evidence/native-provider-profile-cli-local-provenance.json).
Exact CLI source `ac69c1f2a2c4a757b23d0d9da8b98d6188fab3a8` subsequently
passed its hosted **65 native contracts in 149.37 seconds**, **94 extension and
17 browser tests**, native/browser integration and VSIX verification, with zero
failures/skips. [Hosted CLI evidence](evidence/native-provider-profile-cli-hosted-provenance.json).
That hosted gate excludes the newer Claude66 agent/metrics and Claude67 history
source. No live Gemini
key, external account acceptance or installed-preview upgrade is claimed.
The actual browser/native integration passed the four default routes and an
inactive Gemini profile save against the rebuilt server, together with durable
refresh, adapter/backend restarts and the existing real graph/file/history flow.
It uses synthetic provider fixtures and does not establish live Gemini inference
or rendered IDE enrollment.

The earlier gateway/history source `75f45f0` passed its exact hosted gate:
**61 native, 91 extension and 17 browser contracts**, native/browser integration
and VSIX verification, with no failures or skips.
[Hosted provenance](evidence/native-gemini-history-hosted-provenance.json).
The subsequent `c9591fe` agent milestone passed **62 native contracts locally in
100.44 seconds**, verifying real AgentRunner/file/SQLite execution with synthetic
provider replies. [Local scope](evidence/native-gemini-agent-local-provenance.json).
Exact revision `c9591fe79cad9a4253ac8088933f0c8a2848ded1` also passed its hosted
**62 native, 91 extension and 17 browser contracts**, native/browser integration
and VSIX verification, with zero failures/skips. Its native gate took **128.80
seconds**. [Hosted scope](evidence/native-gemini-agent-hosted-provenance.json).
These earlier gates exclude the new catalogue/profile enrollment source. See
[validation status](VALIDATION_STATUS.md) for the remaining acceptance boundary.

## Generic provider-profile CLI controls

The native CLI exposes the same backend-owned profile API as Settings:

```text
xmind_cli PORT provider-profiles
xmind_cli PORT profile-models ID ROUTE REVISION [KEY_ENV]
xmind_cli PORT save-profile ID ROUTE MODEL REVISION [KEY_ENV] [--activate]
xmind_cli PORT select-profile ID REVISION
xmind_cli PORT provider-models
```

`provider-profiles` returns actual public metadata and advertised routes.
`REVISION` is its registry revision, not the individual profile's saved version.
`profile-models` discovers for that profile/route snapshot without persisting a
draft key. `save-profile` publishes a validated profile; a new inactive profile
does not replace shared execution. `--activate` explicitly requests activation
and must be the final argument. Updating an already active profile preserves
its active identity. `select-profile` explicitly changes shared selection with
the supplied revision. Setup commands create no conversations, user messages
or agent runs and do not request inference.

Only a privately inherited environment-variable name, `KEY_ENV`, is accepted in
command arguments. The CLI forwards its value in the authenticated request;
the backend owns encrypted persistence through xlang3 SQLite. Public responses
exclude raw keys, credential references and provider destinations. Reserved
authentication/UI variables cannot be used as provider-key variables. Omitting
`KEY_ENV` omits `api_key` and reuses the saved profile's owned credential; a new
profile still requires a key. Discovery/save retain full returned model resource
identities, including Gemini's `models/<id>` prefix, and cannot substitute a
display label or infer tool capability from catalogue generation methods.

No-argument `provider-models` prefers the active profile's saved-key discovery
on a profile-capable backend. The existing `provider-models KEY_ENV REVISION`
form remains the legacy setup API. The older single-OpenAI compatibility
configuration commands retain their own scope.

Inside chat, `/profiles` reads fresh metadata without rebinding the chat's
admission snapshot. `/provider-models` discovers the freshly observed active
profile/route/revision using its saved key, also without rebinding admission.
Only explicit `/profile ID REVISION` selection updates that binding from the
returned committed metadata and clears the local model override. Stale
selection/discovery reports failure, retains the prior binding/model and sends
no automatic retry. External changes cannot be silently adopted by a listing
or discovery command.

Selection preserves existing history; it does not implement conversion of
foreign signed/provider/tool receipts. Fresh conversations and compatible text
histories can continue on the selected wire. A Gemini receipt mapped into
Chat/Claude, or older Chat tool calls without their original Gemini receipt,
are rejected by native history validation. Recorded history and tool outcomes
remain preserved. Full cross-wire receipt mapping/acceptance is still required;
generic profile controls do not establish arbitrary session continuation across
every provider. Successful chat settings commands clear only prior settings
errors and preserve the last failed/cancelled model turn's exit status. Discovery
has a 35-second CLI read deadline and restores ordinary request timing afterward;
redirects and automatic retries remain disabled.

The CLI contract passed in **4.23 seconds** as part of the preceding local 65-contract
gate. It runs the compiled CLI against the authenticated native server and
embedded xlang3 SQLite with labelled synthetic provider peers. It verifies
private key input/reuse, full resources, inactive save/selection, registry races,
encrypted reopen/rollback, bounded safe diagnostics and failed-turn status
through successful settings commands. All new source cases were compiled and
passed; the preceding hosted 64-contract gate excludes these additions.
The exact hosted CLI65 result is recorded above; it excludes the later Claude66
and Claude67 source. Live account and new installed-preview acceptance remain
pending.
[CLI syntax, behavior and historical evidence](native-interactive-cli.md).

The sections below preserve earlier provider-setup checkpoints and their limits.
Their pending statements describe those revisions; current Claude/Gemini source and
verification are summarized above. The installed preview retains native
`19d69dd` and view `6f32d215`; historical preview references below describe earlier
installations.

## Run-descriptor attribution

The shared browser/VS Code renderer now has an admitted-profile row beneath the
recorded-run selector. It uses only the selected descriptor's saved context and
keeps historical attribution independent of today's model selector. Its tooltip
shows the profile ID/version and route; the row displays provider/wire and model.
Failed and cancelled runs can expose admission identity without an assistant
response or invented usage. Missing/malformed context hides the row; changing
conversation or resetting run observation clears it. Local verification passed
**88 extension and 17 browser tests**, plus actual native/browser integration
against the existing hosted `19d69dd` binary. That integration proves backward
compatibility. The descriptor projection subsequently passed the cleanup's
hosted gate, recorded below. This view source
has not replaced the installed preview.

New native source projects `provider_context` into admitted, inspected and listed
run descriptors, including graph roots and agent children. Repository reads use
the first user message owned by that exact run, through xlang3 SQLite. They do
not consult the current profile or unrelated session messages. The existing
durable admission receipt remains the source of truth; no database migration or
client-owned metadata is introduced. Absent historical context stays absent.

The repository validates the exact six public context fields before admission
and when reading them. Unexpected credential fields, invalid profile versions,
identities or wire APIs fail closed. Graph child completion excludes this backend
metadata from dependency outputs sent to later model nodes, while retaining it
in child history and run descriptors.

Native tests now cover API admission receipts, descriptor projection across
updates/reopen, unrelated legacy messages, malformed-context rejection without
partial admission, and two dependent agent nodes with an independent peer
checking that profile metadata never enters model requests. The complete cleanup
gate at `dea5888` passed **58 native, 88 extension and 17 browser contracts**,
native/browser integration and VSIX verification with no failures/skips.
[Exact hosted evidence](evidence/native-cleanup-hosted-provenance.json).
This verifies descriptor projection, graph dependency filtering and the run
inspector; it excludes newer Gemini stream/transport/history changes. The
installed preview remains on native admission checkpoint `19d69dd` and view
`6f32d215`; the cleanup bundle has not been installed.

## Recovering from another view's provider change

On an admission HTTP 409, a client with a profile binding re-reads public
profile metadata. If the revision or active profile differs from the submitted
binding, it clears any unsaved key draft and refreshes the displayed profile,
wire API and backend-enabled models. It tells the user to review the current
profile and submit again. The prompt remains in the composer; the rejected task
is never retried or appended as an accepted user message.

Unchanged-profile conflicts retain their original error. Transport failures do
not trigger task replay. Disposed views and obsolete backend generations cannot
publish a late reconciliation. These paths passed **86 extension and 17 browser
tests**, including actual host-controller fixtures for both clients. This is
client behavior evidence, not a new native or live-account acceptance claim.

## Durable provider context

Exact checkpoint `6f32d215493122b433d6e20ff65bfce6c9120c27` passed its full
hosted gate: **57 native, 86 extension and 17 browser contracts**, native/browser
integration and VSIX verification, with no test skips.
[Hosted run](https://github.com/xlang-foundation/xMind/actions/runs/37731334471),
[unaltered native log](evidence/native-provider-history-hosted-ctest.log),
[job/TAP output](evidence/native-provider-history-passing-ci-job.log) and
[exact provenance](evidence/native-provider-history-hosted-provenance.json)
establish that component scope. Later descriptor projection, graph dependency
filtering, run-inspector UI and the original Gemini request component passed the
separate cleanup gate above. Newer Gemini source has its own
[validation scope](VALIDATION_STATUS.md). The live
preview still uses the previously installed `19d69dd` native binary.

New source captures a public `provider_context` in the admitted user message
and each persisted model response. It records profile ID, profile version,
route ID, provider family, wire API and selected model. These values come from
the immutable native execution configuration, not from client-supplied labels.
Profile version identifies that profile's saved configuration; it differs from
the registry revision used for admission conflicts. Credentials, credential
references and endpoint URLs are excluded. Provenance fields are not serialized
into model conversation requests.

Graph roots retain their admission context; agent children record their actual
selected model, including node overrides. Incoming A2A tasks retain context in
their original durable history; an idempotent replay returns the original run.
Tool children do not receive invented model provenance. A graph root's selected
provider context does not imply that its human/tool nodes called a model.
The context is stored atomically with existing message/graph admission records
through xlang3 SQLite. At checkpoint `6f32d215` it was not a field on the public
run descriptor; the later projection is verified by the cleanup gate above.

The shared renderer displays the recorded provider/wire beside response metrics,
with profile version and identity available in its tooltip. Old history with no
context remains unattributed; it never borrows today's selected profile.
Malformed context is suppressed without inventing tokens or profile labels.
Initial local verification passed **83 extension and 16 browser tests**. The
later full hosted checkpoint above passed native checks across profile updates,
reopen, graph agent children and incoming message replay. The preview has not
been upgraded to that native history-context binary.

## Profile binding at task admission

New source adds optional `provider_profile_id` and
`expected_provider_revision` fields to agent and graph admission. Both must be
present together. The native profile runtime checks them under the same lock
used to change profiles and admit work; a stale binding returns HTTP 409 before
creating a run or appending its prompt. An empty active profile remains valid
for model-free graphs. Health advertises agent and graph support separately;
implementations without support refuse explicitly bound requests.

Browser and VS Code tasks carry the profile snapshot already shown in their
view. Interactive CLI captures its snapshot when chat starts; one-shot CLI
reads metadata before submission. Updated clients send bindings only when
the native backend advertises support. Older unbound clients retain their
existing behavior; this is not universal enforcement for every caller.

Local client validation passed **82 extension and 16 browser tests**, without
skips. New native contracts cover stale, malformed and current HTTP bindings,
model-free graph admission, and a compiled CLI race through a loopback proxy
which switches the actual native profile before forwarding the request. These
contracts passed in the full hosted gate at exact source
`19d69dd0876e0a01accb444e06924f36008f8ee6`: **57 native, 82 extension and
16 browser contracts**, actual browser/native integration and VSIX verification.
[Hosted run](https://github.com/xlang-foundation/xMind/actions/runs/37730034170),
[original native output](evidence/native-provider-admission-hosted-ctest.log),
[job/TAP output](evidence/native-provider-admission-passing-ci-job.log) and
[provenance with bundle hashes](evidence/native-provider-admission-hosted-provenance.json).

That exact native bundle is installed at the existing webpage/backend origins.
The view uses source `6f32d21`, separately passing 86 extension and 17 browser
tests plus actual integration against the hosted native bundle
([integration output](evidence/provider-admission-latest-view-integration.log)).
The closed-database backup and upgrade preserved all 10 sessions, 14 terminal
runs and 33 history messages by exact fingerprint, provider revision 3 and the
durable browser cookie. Actual browser reload restored saved history, the
OpenAI Responses profile, Agent default and the gpt-5.6-sol footer chooser
([metadata-only live record](evidence/live-provider-admission-upgrade.json)).
No new key or inference was submitted. Later native history attribution passed
its separate `6f32d21` hosted gate and remains absent from the installed binary.
Public run-descriptor attribution, live Claude acceptance and actual VS Code
profile UI acceptance remain outstanding.

## Shared profile Settings controls

Exact source `58a7595ce24a6b5015a9bb7be9698105910a8c48` passed the complete
hosted gate: **57 native, 80 extension and 15 browser contracts**, actual
browser/native integration and VSIX asset verification, with no test skips.
[Hosted run](https://github.com/xlang-foundation/xMind/actions/runs/37728813956),
[native output](evidence/native-shared-profile-ui-hosted-ctest.log),
[original job/TAP output](evidence/native-shared-profile-ui-passing-ci-job.log)
and [exact provenance](evidence/native-shared-profile-ui-hosted-provenance.json)
retain that scope. These results do not cover the later admission/context source.

Settings now has a saved-profile selector, an Add profile choice, a provider API
selector and a Use saved profile action. OpenAI Chat/Responses and Claude
Messages routes come from validated native metadata. New profiles receive an
automatic identity; users do not enter model IDs. Keys are consumed privately by
the browser/extension host, and models returned by account discovery appear in
the sidebar footer. A footer choice calls native save/activation before updating
the model view. Switching a saved profile calls native selection with its known
revision; running/paused ownership conflicts remain backend decisions.

Both hosts use `ProviderProfileController`. Its unsaved-key draft expires after
five minutes; backend/conversation changes invalidate it and late discovery does
not publish into a retired view. Saved-key discovery stays on the profile's
owned route; a later native save can rebind a model within that provider family.
The prior single-OpenAI setup path remains for an explicitly missing profile API.

Local validation passed **80 extension tests**, including profile controller,
renderer and legacy host contracts, **15 browser tests**, including the new
Settings/controller contract, and the actual browser/native integration.
Synthetic controller/DOM fixtures do not prove live account inference. Live
Claude, actual VS Code profile UI acceptance, and atomic provider-profile revision
binding on run admission remain required. View source `58a7595` was installed at
the existing webpage origin, retaining the native process and durable cookie.
Actual browser inspection after reload confirmed saved OpenAI configuration,
Add profile and Claude Messages setup with an empty key field, while the model
chooser stayed in the footer. Provider revision 3 and the original profile were
preserved. No new credential or inference was submitted
([metadata-only acceptance](evidence/browser-provider-profile-settings.json)).

## Profile runtime in server startup

The shared browser/VS Code `BackendClient` now exposes profile metadata,
profile-specific discovery, save and selection operations. It validates public
metadata, unique identities, route/family consistency, safe revisions and model
catalogues before returning them to a view. Unknown/secret-bearing metadata is
rejected. Omitted keys remain omitted on the wire; account discovery has a
35-second client deadline for the native adapter's bounded 30-second request.
The browser gateway permits only the exact native profile paths and preserves
its existing origin/authentication rules.

All **76 extension and 14 browser tests** passed locally. The actual compiled
native/browser contract additionally saved and selected an independently keyed
profile, checked inactive-profile behavior and stale revisions, preserved graph
history, and exercised profile reads/mutations through an origin-bound durable
cookie. Profile/model values and keys in this contract are synthetic; it does
not request inference. Visible profile Settings controls are still pending.

The model-free Windows server now constructs `ProviderProfileRuntime`, registers
the authenticated profile API and imports legacy OpenAI setup before listening.
Backend routes are fixed OpenAI Chat/Responses and Anthropic Messages with their
account catalogue endpoints. Explicit `--model` startup configuration keeps its
existing execution path. Client-supplied destinations remain prohibited.

`ProviderProfileLegacySetup` adapts the existing configuration/discovery endpoints
to this same runtime. It owns no separate execution engine. Existing CLI and
OpenAI Settings clients therefore use the profile registry through their current
API, while the new profile API supports separate keys and active selection.
The legacy adapter retains the existing GPT-family Chat/Responses selection rule;
the profile API chooses explicit backend route identities.

A trusted migration may import an empty-model profile when a legacy model equals
its key or looks like an API key. Its encrypted credential and revision survive,
but no model is advertised and inference remains unavailable. Saved-key discovery
and configuration can repair selection without key reentry. Public enrollment
still requires a valid nonempty model. The legacy source record is retained;
this does not erase plaintext from older SQLite pages or backups.

Local checks passed for the compiled server/CLI profile enrollment, selection,
restart and conversation preservation, and the native profile runtime contract
now starts the actual server on valid and repairable legacy databases. The
existing **73 extension and 14 browser tests** and actual browser/native graph
contract passed against the new compiled backend. Source
`336f40cc4d8918614e8d31c2ef772c627e04707d` passed the complete **57 native
contracts** in **95.10 seconds**, without failures or skips
([CTest](evidence/native-provider-profile-startup-local-ctest.log),
[source/binary provenance](evidence/native-provider-profile-startup-local-provenance.json)).
The actual preview backend was upgraded at its existing origin with a closed
database backup. Its 10 sessions, 14 terminal runs and 33 history entries matched
the private pre-upgrade fingerprint; provider identity and revision 3 were
preserved. The view process stayed running, and a private test cookie remained
authorized across the native upgrade
([metadata-only evidence](evidence/live-provider-profile-upgrade.json)).
A multi-provider client Settings selector and live Claude acceptance remain
pending. No new inference or rendered profile UI is claimed.

## Multiple provider enrollment boundary

The native model-profile registry now has source for independently encrypted
provider keys, explicit backend-owned route/family policy, profile and registry
revisions, saved-key rebinding within one provider family, and independent active
selection. It writes a single registry snapshot through a new exact-payload
SQLite compare-and-swap operation on the embedded xlang3 persistence worker.
Concurrent writers cannot publish over a changed snapshot. Candidate credential
encryption precedes publication; a failed CAS/transaction can retain an encrypted
orphan candidate but preserves all published keys and profile references.

The new native contract uses actual encrypted SQLite storage and restart, key
purpose mismatch, concurrent publication, an actual SQLite trigger failure and
legacy-record preservation. Source `1cc3ee7bfddce9bb963c71031db87859b9c943bf`
passed the full hosted gate: **56 native, 73 extension and 14 browser contracts**,
with no failures or skips ([CI](https://github.com/xlang-foundation/xMind/actions/runs/37717927955),
[CTest](evidence/native-provider-profiles-hosted-ctest.log),
[provenance](evidence/native-provider-profiles-hosted-provenance.json)).
These verified backend components do not change the existing provider HTTP API,
activate an execution service, migrate the existing OpenAI record or make Claude
available in Settings. Runtime/service publication, migration, discovery and both
client adapters still need integration and acceptance. The hosted native gate's
exact expected set includes the new profile contract (56 total).

The next registry change adds a backend-only validator before durable publication
and an initial import operation that retains an existing encrypted credential
reference and configuration revision. The validator can construct the native
execution service before the registry CAS; its failure leaves the published
profile unchanged. Service publication remains the owning runtime's responsibility
after successful CAS. Initial migration cannot replace an existing registry or
rewrite the legacy source record. Actual SQLite/key/restart contracts now include
native service-construction rejection and two concurrent migration candidates;
these additions passed the local and hosted 57-contract native gates recorded
below. Product startup and client enrollment remain pending.

New `ProviderProfileRuntime` source now connects the registry to the same native
single-agent/graph execution platform. It validates candidate services, commits
profile changes with CAS and then publishes the selected service under its
admission lock. Saving an inactive profile preserves the running selection;
updating the active profile rebuilds its service. Healthy idle ownership is
required, including for paused graph roots. Startup restores the selected profile
and its backend-bound encrypted key. The native contract uses independent OpenAI
and Claude HTTP peers, actual xlang3/SQLite storage, an SQL publication fault,
paused-graph input, restart and migration of an existing key reference. Source
`62a5cb901485b4b7335ad6cc9d72194d21b42789` passed all **57 native contracts**
locally in 91.31 seconds, with no failures or skips
([CTest](evidence/native-provider-profile-runtime-local-ctest.log),
[provenance](evidence/native-provider-profile-runtime-local-provenance.json)).
The build uses the correctness/performance-validated xlang3 buffer-fix pin
`4aea7d8fb24da9ba86f9d7eeb92820794f213d29`. The same source passed the hosted gate:
**57 native, 73 extension and 14 browser contracts**, without skips
([CI](https://github.com/xlang-foundation/xMind/actions/runs/37724610441),
[CTest](evidence/native-provider-profile-runtime-hosted-ctest.log),
[job](evidence/native-provider-profile-runtime-hosted-job.log),
[provenance](evidence/native-provider-profile-runtime-hosted-provenance.json)).
The server's startup and client adapters still need integration. This source
does not upgrade the running preview or establish live Claude account support.

Profile-specific account discovery is now implemented in source for OpenAI and
Claude. C++ owns the catalogue endpoint and authentication format. Omitted keys
resolve only the named profile's own route-bound credential; supplied keys are
used for discovery without storage. Discovery checks the registry revision before
and after network I/O and never holds the admission lock during that I/O.
Claude pages use bounded, encoded `after_id` cursors as documented by the
[Claude Models API](https://platform.claude.com/docs/en/api/models/list), with
eight pages, 4096 entries and a 30-second total deadline. Malformed or duplicate
JSON fields, repeated cursors, reflected keys, incomplete oversized catalogues
and redirects fail rather than returning an incomplete list. The expanded native
peer contract includes actual saved-key requests, pagination, URI encoding,
cancelled discovery and a profile change during an in-flight request. These new
additions passed the complete local 57-contract gate at source `3e2198a` in
94.71 seconds, without failures or skips. Listing account identities does not
establish model/adapter capability; product Settings integration and per-model
compatibility remain pending.

The native HTTP adapter now has source for authenticated profile operations:
`GET /v1/provider/profiles`, `POST /v1/provider/profiles`,
`POST /v1/provider/profiles/select` and `POST /v1/provider/profiles/models`.
Public responses contain profile identities, revisions, selected models and
fixed backend route identities/wires; they omit credential references, keys and
network destinations. Mutations reject unknown fields, malformed revisions and
non-boolean activation flags. Existing origin-bound view authentication covers
the new paths using the same owner permissions as provider setup. The expanded
native contract exercises actual HTTP authentication, enrollment, saved-key
discovery, destination spoof rejection and stale selection. These additions
passed that local 57-contract gate. Server startup and CLI/browser/
VS Code client integration remain required before these become product Settings.

`ProviderProfileRuntime::import_legacy_configuration` adds the trusted native
reader for the existing `native-provider/active` record. It validates bounded
JSON, duplicate/unknown fields, provider, endpoint/wire and revision against
backend policy, then imports using the existing credential-ownership and
candidate-service/CAS checks. It preserves the original encrypted reference,
revision and legacy source record. Missing legacy state is a no-op; an existing
registry wins over stale legacy state. Invalid legacy models are rejected;
credential-shaped legacy-model repair still needs a product compatibility path.
This reader is not yet called by server startup. Source
`0238da0bab9296b70c67281d923a1d17d440e791` passed all **57 native contracts**
locally in **93.22 seconds**, without failures or skips
([CTest](evidence/native-provider-profile-api-migration-local-ctest.log),
[source/binary provenance](evidence/native-provider-profile-api-migration-local-provenance.json)).
The full gate covers discovery, authenticated profile APIs and this reader.
The test fixture was corrected to respect the database's JSON constraint before
that complete passing run. No preview upgrade or product profile UI is claimed.

At the early single-provider checkpoint, setup was one saved OpenAI configuration.
Claude request/stream/transport additions had not yet changed that API or made
Claude selectable in Settings.
The next enrollment change must retain the existing encrypted OpenAI key and
model while adding an independently identified provider profile. C++ owns the
allowed endpoint/wire/header policy and per-model capabilities; a client selects
a provider/profile and discovered model, never an arbitrary credential
destination. Model-name prefixes alone must not choose a provider or key.

Each profile needs its own credential binding, configuration revision and model
catalogue. Discovery with a supplied key has no enrollment side effects. Discovery
with an omitted key resolves only that profile's encrypted credential. Updating
one profile must preserve every other profile and fail on stale revisions or
active work before publication. Existing single-provider records must migrate
without changing their encrypted credential binding or routing. Each admitted
run must snapshot provider/profile/model identity so later Settings changes cannot
redirect its continuation or replay opaque history through another wire.

Browser and VS Code Settings will offer provider-specific key configuration, with
account models in the existing bottom composer chooser. Native CLI uses a private
environment source for key enrollment. No key is placed in the sidebar, command
arguments, public metadata or model context. Backend storage remains SQLite I/O
through embedded xlang3; C++ owns credential encryption and routing. Migration,
restart, cross-profile key isolation, revision races and actual live inference
are required acceptance boundaries; this section specifies pending work.

Private key replacement source `6c423c57d0f0ac16678b8b85c8ae95779ed11360` subsequently passed **43 native and 48 extension contracts** in [CI](https://github.com/xlang-foundation/xMind/actions/runs/37669285942); [original job/TAP](evidence/native-private-key-replacement-passing-ci-job.log). This verifies the adapter's replacement flow separately from successful real-account acceptance, which is still waiting for the private key prompt.

Next CLI source adds `xmind_cli PORT provider-models` to discover account IDs with the encrypted backend key. `provider-models KEY_ENV REVISION` discovers using a privately named environment source for new setup. Keys never belong in command arguments; both discovery and configuration share the reserved-variable/revision validation and clear the source only inside the client process. An actual native CLI/HTTP/WinHTTP interoperability fixture is prepared for both paths, stale rejection and attempted authentication-token substitution. This addition is not yet compiled or installed.

Installed backend `5815141dcf27652a1af503e39f17127ba189c403` passed **43 native and 47 extension contracts** in [isolated CI](https://github.com/xlang-foundation/xMind/actions/runs/37665810113), with no skips: [CTest](evidence/native-model-discovery-saved-key-hosted-ctest.log), [original job/TAP](evidence/native-model-discovery-saved-key-passing-ci-job.log), [provenance](evidence/native-model-discovery-saved-key-hosted-provenance.json). Native tests include encrypted saved-key discovery after restart, key-free model selection, legacy credential-shaped model withholding/repair and direct rejection of credential-as-model enrollment. The actual preview was upgraded with its closed database backed up and draft/profile retained. A live discovery request reached OpenAI but its stored credential was rejected for authentication/authorization. The actual replacement password prompt is open. **No successful account catalog or live inference is claimed.** [Actual launch and separate 48-contract local adapter verification record](evidence/vscode-model-discovery-launch.json).

Discovery checkpoint `9c4ef46c3c55c0ac83640b206ca59bdd1a7bf386` passed **43 native and 46 extension contracts**, with no skips, in [isolated CI](https://github.com/xlang-foundation/xMind/actions/runs/37663822234). [Original CTest output](evidence/native-model-discovery-hosted-ctest.log), [original job/TAP output](evidence/native-model-discovery-passing-ci-job.log), [provenance](evidence/native-model-discovery-hosted-provenance.json). Its synthetic native peers verify actual GET/authentication, returned IDs, malformed responses, credential-safe errors, forbidden redirects, response limits, cancellation and deadlines. Saved-key reuse and legacy repair also passed at the installed checkpoint below.

The next source replaces manual model-ID entry with a native account-discovery request and a searchable VS Code picker. New setup asks for a password; existing setup reuses the encrypted backend credential, without sending it to the editor. The host posts `POST /v1/provider/models`; C++ sends a bounded, authenticated GET to the fixed [OpenAI model-list endpoint](https://developers.openai.com/api/reference/resources/models/methods/list). Only validated, sorted, deduplicated IDs are returned. Discovery has no persistence or configuration side effects. Redirects are refused; provider error bodies and arbitrary metadata are not forwarded. Empty/malformed lists, discovery failures and dismissed/stale prompts do not enroll a model. The verified saved-key backend is now installed; the private key-replacement adapter adds a separately verified 48th local extension contract.

Discovery and enrollment may omit `api_key` only to use the existing credential at the expected revision. New model identities cannot look like OpenAI keys or equal the selected credential. Older accidental key-as-model enrollment is withheld from public model metadata and execution while retaining the encrypted key for discovery and repair. This does not claim secure erasure of historical SQLite pages or backups.

Source `86ff065cafeb0b43f9bc47adb2d16b631dacade3` passed **42 native and 44 extension contracts** in [isolated CI](https://github.com/xlang-foundation/xMind/actions/runs/37657184396): [hosted CTest](evidence/native-provider-setup-hosted-ctest.log), [original job/TAP](evidence/native-provider-setup-passing-ci-job.log), [hosted provenance](evidence/native-provider-setup-hosted-provenance.json). At that earlier checkpoint the contract-tested bundle was installed in the actual right-sidebar preview, and its model-ID input prompt opened against the authenticated native backend. Existing sessions/profile and the user's unsent draft were preserved, with a closed-database backup before migration. [Launch/provenance record](evidence/vscode-provider-setup-launch.json). No provider key was supplied by the agent and no live-provider response is claimed. Earlier [local adapter output](evidence/vscode-provider-setup-local.log) remains preserved.

At the initial enrollment checkpoint, a model-free native server owned a `ProviderRuntime`. Authenticated full-access local owners could inspect `/v1/provider/configuration` and submit a model ID, API key and expected configuration revision. The backend fixed the destination to OpenAI's Chat Completions endpoint; the request could not replace the endpoint, workspace or effect policy. Existing startup-configured services continued using their explicit backend configuration. Enrollment exposed one model at that checkpoint; broader provider catalogs, wire families and account lifecycles remained required by the project goal.

The VS Code host obtains the key through a password input and models through the backend discovery response. A view message can request setup but cannot supply its own key or destination. The host rechecks backend origin, controller generation and the supported endpoint across discovery and selection before enrollment. The provider key is not put in webview messages, editor state or extension SecretStorage. Backend authentication remains a separate origin-scoped SecretStorage token. OpenAI's list gives account model identities, not endpoint or tool capabilities. The current inference adapter uses text Chat Completions; listing a model does not establish compatibility with that adapter. An actual submitted response remains required to verify inference.

The native backend stores the credential encrypted in its existing credential repository, using embedded xlang3 for SQL. Each configuration candidate receives a new credential identity bound to the backend endpoint. Configuration persistence precedes publication of the new service; failed candidate persistence leaves the earlier credential reference/service intact. Unreferenced encrypted candidate credentials are retained, including after a process interruption; credential lifecycle/cleanup remains pending. The public configuration record stores only provider/model/endpoint/revision and a backend credential reference. API metadata omits the credential identity and key.

Changes are allowed only while the current service is healthy and has no admitted/running jobs. Admission, cancellation and provider changes share the runtime's ownership lock. A persisted revision rejects stale enrollment. Startup restores the saved provider/model and resolves the encrypted credential against the backend policy, failing closed on corrupt or mismatched records. Workspace/MCP/process/instruction settings remain immutable backend startup policy.

The native contract passed actual HTTP authentication and setup, destination-spoof rejection, encrypted xlang3 storage, active/stale configuration conflicts, native provider transport, actual SQL update failure preservation and persisted reopen. Its provider/key are explicitly synthetic test data. Interactive completion with the user's key and an actual provider response remain the next acceptance boundary.

The native CLI supports `xmind_cli PORT provider` for metadata and `xmind_cli PORT configure-provider MODEL KEY_ENV REVISION` for enrollment. The private key is read from the named process environment variable, rather than a command argument, and the client clears that variable in its own Windows process after copying it. The backend still owns validation, revision checks, encryption and service publication. A separate native server/CLI contract passed for enrollment, missing/stale rejection, encrypted storage and restart; it uses a synthetic key and never calls external inference. CLI source `1844b5297826951d28646714d7139fd546137328` passed the complete 43 native and 44 extension contract set in [CI](https://github.com/xlang-foundation/xMind/actions/runs/37659261478), with [CTest](evidence/native-provider-cli-hosted-ctest.log) and [provenance](evidence/native-provider-cli-hosted-provenance.json). The installed discovery bundle includes these commands.
