# Native model eligibility

OpenAI profile discovery now returns the intersection of the account catalogue
and a backend-owned model policy for the selected wire. The account's model
list provides identities, not function-calling or streaming capability facts.
The default policy declares 21 exact aliases and snapshots across GPT-6,
GPT-5.6, GPT-5.5, GPT-5.4, GPT-4.1 and GPT-4o. It does not match naming prefixes,
invent capability metadata from discovery, or silently switch a profile's route.

| Model family | Responses with workspace tools | Chat with workspace tools |
| --- | --- | --- |
| GPT-6 Astra, GPT-6.1 Sol | Supported | Unsupported; use Responses |
| GPT-6 Sol, GPT-6 Luna | Supported | Requires explicit `reasoning_effort: none` |
| GPT-5.6 Sol/Terra/Luna and Sol alias | Supported | Supported |
| Declared GPT-5.5, GPT-5.4, GPT-4.1 and GPT-4o identities | Supported | Supported |

The rules follow the official [function-calling guide](https://developers.openai.com/api/docs/guides/function-calling),
[GPT-6.1 Sol](https://developers.openai.com/api/docs/models/gpt-6.1-sol),
[GPT-6 Sol](https://developers.openai.com/api/docs/models/gpt-6-sol) and
[GPT-6 Luna](https://developers.openai.com/api/docs/models/gpt-6-luna) pages,
consulted on 2026-10-08. The remaining identity and reasoning facts come from
the corresponding official model pages. Account availability still controls
which declared models discovery can return. A passing model listing does not
establish that every generation request will succeed for an account.

The native policy binds tools, streaming usage, output-limit support and
supported reasoning efforts. Chat's mandatory tool reasoning is supplied only
when the workspace requires tools. An explicitly conflicting reasoning choice
is rejected rather than replaced. Tool-free generic Chat can still use Astra
or GPT-6.1 Sol. Responses retains its configured reasoning choice, or leaves
reasoning unspecified when none was configured. Nonreasoning GPT-4 models
reject an explicit reasoning effort.

The same validation runs before profile save, YAML batch encryption, trusted
legacy import and preparation of an active runtime. Startup validates inactive
stored text identities and reasoning too; workspace tool requirements are
checked when a profile becomes active. Existing inactive text profiles from
other providers can still reopen beside an active coding profile. Draft and
saved-key discovery apply the same eligibility
rules without persisting credentials or activating a model. Run admission
continues to require the actual selected profile, model and workspace generation.
Invalid choices cannot dispatch a provider request or admit a prompt/run.

`ProviderModelPolicy` is a native declaration boundary. A trusted backend can
declare additional deployment identities with explicit capabilities. Clients
cannot supply this policy. The built-in table leaves unlisted future versions,
fine-tuned IDs, embeddings, image and audio models unavailable through these
streamed-text routes until a corresponding native declaration is added.
Other providers retain their existing route policies. This change does not
claim complete provider parity or account acceptance for all 21 identities.

The independent fixture checks authenticated Chat/Responses filtering and
saved-key discovery, exact required reasoning in both native Chat requests,
a real read from its temporary workspace, encrypted SQLite credentials,
rejection before encryption/admission, atomic mixed YAML rejection,
trusted legacy-import rejection, inactive stored-model rejection, and byte-exact
history after reopen. Its provider replies and credentials are synthetic.
The separate request contract verifies Responses serialization, incompatible
reasoning/wires, and a trusted explicit custom deployment declaration.

The complete local gate passed **89/89 native contracts in 172.73 seconds**,
with all 564 source inputs unchanged and no exclusions or skips. The first
attempt failed compilation because the new fixture used an unavailable empty
credential constructor. The next complete attempt passed 87/89: the older CLI
fixture used an undeclared identity, and startup incorrectly applied active
workspace requirements to an inactive Gemini text profile. Both were corrected;
the failed attempts remain retained. The new native policy runtime contract
passed in both complete attempts.

All 39 frontend sources still match the separate prior **173 extension / 33
browser** passing checkpoint. Those unchanged suites were not rerun for this
native change. The SDK remains the accepted isolated xlang3 branch at
`ad8040ffb8aba6eeabeb09053a8e222df09a4e7a`, including its recorded 41/42
selected-correctness baseline failure. No CPython was run. The older browser
preview is unchanged. New package and live model acceptance remain separate.
[Native source and retained-attempt evidence](evidence/native-model-eligibility-provenance.json).
