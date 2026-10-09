# Responses failures and real CLI file writing

The native Responses parser now records a bounded classification for
`response.failed`, `response.incomplete` and streaming error events. Known
server errors, rate limits, output token limits and content filtering have
specific failure codes and client messages. Unknown classifications remain
`other`; provider messages, arbitrary metadata and output are not reflected
into the diagnostic. The terminal response identity and status must match
the active stream before its classification or usage is published.

Valid provider-supplied failure usage is preserved as a `model.usage` event.
Only known token counters are copied, with integer bounds, total consistency
and cache/reasoning limits checked. Absent, unavailable or invalid usage remains
unavailable. No completed assistant message or successful turn is invented.
An incomplete output item can reach its terminal failure event for diagnosis,
but cannot become a completed response or executable tool call. Failure does
not silently retry inference or replay an effect.

The client renders actionable failure messages and actual failed-response
usage. This is shared source for the VS Code and browser views; it has not
replaced the installed extension while its migration is pending.

The complete local gate passed **98 native contracts in 213.66 seconds**, with
631 mapped inputs unchanged and exact expected/registered/passed contract
names matched. All **199 extension and 39 browser tests** passed, with 41 view
inputs and 11 browser assets unchanged. Native HTTP/CLI fixtures verify persisted
server-error classification, exact supplied usage and rejection of an incomplete
tool call without an operation, assistant history or automatic retry. These
protocol fixtures have synthetic provider replies.
[Tested source and runtime](evidence/native-responses-failure-local.json),
[complete native output](evidence/native-responses-failure-local-ctest.log).

A separate **real OpenAI `gpt-6.1-sol` acceptance passed** using the source-built
native server and CLI, embedded-xlang3 SQLite and the verified pinned pure
library/native package bundle. In an owned temporary workspace, native approval
created a file, another approval edited it, and denial of a third proposal
preserved the edited bytes. All three runs completed through actual provider
continuation, with two supplied usage events per run. Recorded operation
receipts, observed file hashes and final filesystem bytes agree. The adapter
did not write the tested file, read provider keys or execute CPython.
[Live per-run receipts and usage](evidence/live-native-cli-writing.json).

Earlier attempts remain retained under ignored acceptance evidence. An old
runtime's edit failed with `responses_provider_incomplete`; its parser discarded
the classification, so the original cause remains unknown. The successful
later request does not retroactively diagnose that failure. Two test-driver
mistakes were corrected: using uncertain-edit inspection to review a proposal,
and confusing the model tool name `edit_file` with the native approval operation
`replace_file`. Neither mistaken attempt approved an edit.

This proves the recorded native CLI create/edit/deny sequence. It does not
prove installed VS Code writing, rendered approvals, every provider/model,
failure recovery or full coding parity. TestProj and its installed saved profile
were not modified by these acceptance requests. Installed version **0.1.1**
still uses the retained legacy source until the migration is confirmed; that
pending operation's target was not overwritten.
