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
