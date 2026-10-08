# Live provider compatibility investigation

The browser backend was updated to exact CI-tested native revision
`d0a70fef888c3724c2490bcb2cf8ef38d60f08c0` at the same origin and with the same
token. Preflight found no queued/running/paused work. The original runtime and a
private closed-database backup were retained. Provider revision/model, one
session, five recorded runs and seven history entries were unchanged after
startup. The browser adapter stayed running, and actual browser history refresh
worked without a login prompt. Both VS Code previews were left unchanged.

A subsequent live native CLI request in a separate new conversation used the
existing `gpt-5.6-sol` selection and saved backend credential. It failed with
HTTP 400, `invalid_request_error`, and parameter `reasoning_effort`. This is live
failure evidence, not a working model claim. The provider key was not copied to
the client. No raw provider error message is recorded in public evidence.

The official [migration guide](https://developers.openai.com/api/docs/guides/migrate-to-responses)
states that Chat Completions tool calling from GPT-5.4 onward requires reasoning
effort `none`. The [reasoning guide](https://developers.openai.com/api/docs/guides/reasoning)
describes model-specific effort values and Responses tool support. These pages
were opened on 2026-10-07. The recorded parameter and this restriction identify
the relevant compatibility boundary; the raw provider message was intentionally
discarded, so it is not independently quoted or inferred as an exact message.

At revision `d0a70fe`, the serializers did not expose reasoning controls.
Interactive model enrollment fixed the Chat Completions endpoint, although the
native Responses adapter itself was contract-tested. The required implementation was
explicit native model/wire capability routing, reasoning settings, persisted
wire enrollment and secure reuse/rebinding of encrypted provider credentials
under approved backend endpoint policy. Conversation continuation must retain
its original wire items; an existing Responses history cannot be silently
converted to Chat Completions. Live acceptance must then prove the selected
frontier model, real tool effects and actual usage through the shared views.

After the adapter update, actual browser reload retained login, the selected
model and recorded history. The footer showed the backend's current Chat
Completions wire. Selecting the separately recorded CLI diagnostic conversation
displayed HTTP 400, `invalid_request_error` and `reasoning_effort` in the visible
failure card. No new inference was submitted for this UI check. The screenshot
remains private in ignored launch-state storage. This confirms the shared view
renders the live native diagnostic; Responses enrollment was then pending its
compiled gate and a successful live retry.

Revision `4fc1148` subsequently passed all 52 native and 69 extension contracts,
including Responses enrollment, encrypted saved-key endpoint rebinding, reopen
and real SQLite publication rollback against independent synthetic peers. The
exact verified runtime is installed in the browser preview. Its original model,
conversations, runs and history survived the update. Reselecting the same
`gpt-5.6-sol` through native configuration reused its encrypted key and chose
the Responses wire, without reducing reasoning or selecting another model.

One actual minimal CLI request then completed with `XMIND_PROVIDER_CHECK`:
provider usage was 1,382 input, 8 output and 1,390 total tokens. The browser
refreshed without login and displayed that completed conversation, usage and
Responses footer. [Live evidence](evidence/live-responses-provider-success.json)
records only the public check and sanitized metrics. This proves that specific
model and minimal request through the installed wire, not full coding/tool
acceptance or compatibility for every discovered model. Durable adapter-restart
sessions are newer source and remain pending their compiled gate.
