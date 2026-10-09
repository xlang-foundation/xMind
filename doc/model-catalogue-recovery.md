# Provider catalogue recovery

Source `293f66af8621121785123d75a7e544a5db4dc3ee` passed the complete
[isolated view workflow](https://github.com/xlang-foundation/xMind/actions/runs/37874177783):
**175 extension and 35 browser tests**, with zero failures, skips or cancellations.
The workflow built the production browser assets first, then froze and checked
39 tracked view inputs, two locked vendor files and 11 browser assets.
The downloaded archive digest, committed source bindings, unchanged inventories
and generated asset bytes were independently verified afterward.

## Trigger and behavior

Selecting a saved provider and then a conversation can retire the provider
selection acknowledgement after Native has already published it. The old
adapters read the current provider but lost its discovery catalogue, leaving
only the configured model in the chooser. An in-flight discovery could be
retired in the same way.

After conversation selection, both adapters now refresh the observed profile.
They reuse a valid saved catalogue; if it was retired, they discover models
with that observed profile's saved native credential. History remains available
while discovery finishes. They do not replay the provider-selection CAS,
enroll a model, carry an unsaved key across conversations or retry an effect.
Choosing a model still requires its existing explicit native CAS.

The VS Code host also updates its shared capability snapshot when the profile
changes. A configured profile observed after an interrupted switch can then
accept an explicit user submission with the correct `provider_profile_id`,
`expected_provider_revision` and `model_id` fields. A second conversation or
workspace change still retires late catalogue responses.

Four added deterministic regressions exercise retired selection acknowledgements
and a second selection during recovery in the browser and VS Code host fixture.
They assert the observed revision, no automatic model writes, no replayed
selection and no publication of retired model IDs. The host fixture separately
checks an explicit post-recovery submission. Provider replies and native API
receipts in these tests are synthetic; this is adapter/controller acceptance,
not native inference or a rendered IDE claim.

## Retained attempts

Two pre-fix local regressions reproduced the problem, one in each adapter.
Their summaries are retained. The benchmark guard reported three live SDK
benchmark processes, but those two brief Node tests mistakenly ran rather than
deferring. Subsequent local test/build attempts deferred. No SDK performance
result is claimed from that overlap.

The first hosted gate passed 164/175 extension and 28/35 browser tests. It
exposed missing browser assets, positive filesystem fixtures that assumed the
Windows runner's short-name TEMP path was canonical, and a submission fixture
missing its required provider-admission capability. The second passed 174/175
extension and all 35 browser tests; its remaining assertion used `model`
instead of the actual versioned `model_id` request field. Both complete logs,
source maps and artifact digests remain separate from the final passing gate.

Corrections built assets before tests and explicitly bound their location,
created positive fixtures under the canonical temp parent, and corrected the
advertised capability/request-field assertions. Production canonical-path and
admission guards, intentional alias-rejection cases, test counts and negative
assertions were preserved. No tests were excluded.

## Installed scope

The source-bound hosted `browser.js` asset is published to the existing local
preview. This was a single file replacement with its predecessor retained;
no local build or native/access-adapter restart occurred. The connection
binding and owner-token files are unchanged. Refresh loads the new asset.
The other browser assets and the Native backend remain at their separately
verified checkpoints. All 564 native source inputs still match source
`60475f84` and its paired xlang3 SDK `ad8040f`.

The later verified `516d918` VSIX is now installed in the existing VS Code
profile after a fresh benchmark guard cleared. Complete on-disk package checks
passed and settings bytes were unchanged. Window reload and rendered recovery
acceptance remain pending; installation alone does not establish them.
[Installation scope](evidence/native-ci-staging-installed.json).
The full native workflow remains separate from this Node-only gate. This
increment does not establish broad coding, provider or OpenCode parity.

[Provenance and retained scopes](evidence/native-model-catalogue-provenance.json),
[source/asset verification](evidence/native-model-catalogue-source-verified.json),
[file-only publication](evidence/native-model-catalogue-publication.json).
