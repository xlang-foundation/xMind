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

A second actual `gpt-5.6-sol` Responses run requested `read_file` for the public
repository `README.md`. The native tool's output matched the file exactly, and
the provider continued with `# xMind`, its actual first heading. The browser
displayed the persisted tool request, expandable result and both responses'
provider usage: 1,402 input/19 output for the tool request and 3,830 input/7
output for the final reply. [Read-tool evidence](evidence/live-responses-read-file.json)
records the repository content hash, equality checks and final usage without
private keys, session IDs or provider continuation items. This proves one real
read-tool cycle and its Responses continuation, not live approved edits,
process execution, MCP, A2A or full coding acceptance.

The same verified runtime is now configured with native approved file-edit
tools. A dedicated ignored validation file was initialized with a public marker;
the live `gpt-5.6-sol` agent read it and proposed one literal replacement. The
authorized validation controller checked the exact path, before/after bytes,
content hashes, occurrence count, owning run and expiry before submitting the
native CLI's `/allow` command. The operation recorded `succeeded`, the tool
returned its actual outcome, and an independent disk read matched the expected
final bytes. The provider continued with `XMIND_EDIT_COMPLETED`; the shared
browser displayed all three responses' usage and the successful operation.
[Approved-edit evidence](evidence/live-responses-approved-edit.json) records
sanitized checks and final usage. This was a real model/native-file operation,
not synthetic inference or a manually written final result. Approval was sent
by the authorized validation controller, not claimed as a human button click.
Existing conversations and provider configuration survived the configuration
restart. Other previews were not restarted. Process profiles, MCP/A2A live
acceptance and complete coding parity remain unfinished.

One registered foreground command has now also passed live acceptance:
`gpt-5.6-sol` selected the backend's `git-status` profile with no extra arguments.
After exact native CLI proposal review and approval by the authorized validation
controller, Git exited zero. The operation recorded `succeeded`, its owned
process tree was retired, and retained stdout/stderr matched an independent Git
status invocation. An actual output event, command outcome and per-response
usage rendered in the browser. [Process evidence](evidence/live-responses-git-status.json)
contains sanitized checks. This extends acceptance to one registered foreground
process; shell/background/PTY and broader workflow parity remain incomplete.

The installed native `4203b84` runtime also completed one live Responses request
admitted by the independent official A2A SDK 1.3.0 over A2A 1.0. Task inspection
and retries before/after native restart returned the same durable task without
changing its history, runs or event journal. The shared browser displays its
actual response and supplied usage. [A2A evidence](evidence/live-a2a-responses.json).
This adds one local-owner external-client text flow; remote delegation, team
authorization, broader live A2A coverage and full frontier-provider support
remain incomplete.
