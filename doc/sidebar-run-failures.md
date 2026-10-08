# Recorded execution failures in the shared sidebar

The shared VS Code/browser renderer displays a visible selected-run failure card
from the native `run.failed` event. Provider HTTP failures show the recorded HTTP
status, without guessing the cause or interpolating provider error bodies. Other
failures direct the user to the selected run activity.

The card is execution UI, not an assistant message or invented response. It adds
no token estimates, survives transcript refresh, and clears on run reset,
conversation history replacement, or the next user request. Replaying the same
terminal event replaces the card rather than duplicating it.

Validation: 65 extension contracts and 10 browser adapter contracts passed with
zero skips. Renderer contracts use explicitly labelled DOM fixtures. In the
running browser at port 60405, a page reload restored the authenticated session
without a connection prompt; selecting an existing failed native run rendered its
actual recorded HTTP 400. No provider request was fabricated or retried for this
UI check, and the selected model remained unchanged. The private screenshot is
kept outside public repository artifacts.

This change exposes the outcome; it does not fix the failed model request. The
current native transport intentionally drops error bodies. Safe native provider
diagnostics and diagnosis of that HTTP 400 remain outstanding. Full coding
feature parity and interactive editor acceptance remain separate requirements.

The renderer now accepts the source-prepared native diagnostic fields
`provider_error_type`, `provider_error_code`, and `provider_error_param` and shows
known identifiers in an expanded details block. It independently checks the
allowlists and never renders raw messages or unknown field values. All 66
extension contracts pass with zero skips, including known/unknown diagnostic
payloads. This is DOM fixture validation; the running preview's older native
binary supplies no diagnostic fields, and current C++ compilation/live request
diagnosis remain pending. See [native diagnostics](native-provider-diagnostics.md).

Model protocol failures now have their own explanation and a collapsed
"Recorded model protocol diagnostic" detail when the native backend supplies a
recognized `protocol_error_code`. The renderer checks an exact identifier
allowlist independently of the backend, omits unknown values and raw messages,
and shows no diagnostic detail for other failure reasons. It advises reviewing
recorded tool outcomes before starting another run, because earlier turns in a
failed run may already have executed tools. It does not infer that all effects
were prevented, retry the request, invent an assistant reply or supply metrics.

Local verification of this source passed 73 extension and 14 browser tests,
without skips. The labelled renderer fixture covers known identifiers, unknown
strings/markup and non-string fields, transcript refresh, unchanged unsent
composer text, run reset and omission of assistant/metric cards. Shared browser
assets built successfully. This is source/fixture validation: the installed
preview remains on `6263630`, and the newer native diagnostic producer and live
MCP failure diagnosis still await the compiled hosted gate.
