# Reviewable milestones

## First target: usable local coding in the webpage and VS Code — passed 2026-10-16

The first product acceptance gate is a complete local coding task through both
clients, using the same xMind backend and a real opened project such as
`D:\\CantorAI2026\\TestProj`. Each client must discover the active workspace,
show its sessions and available configured models, submit a prompt, stream the
response and token metrics, invoke coding tools, present scoped file changes
for approval, and verify the approved result on disk. The browser connects to
the local profile without asking the user to copy a server token or provider
key that is already configured. VS Code uses its opened folder/workspace roots
and the same local profile. Add-folder and new-workspace flows must stay clear
and must not silently redirect a session to the xMind source checkout.

Acceptance requires an end-to-end coding prompt against TestProj in the actual
webpage and VS Code, with file changes reviewed and approved in each client,
plus restart/session persistence and cross-client visibility checks. Existing
component tests and synthetic provider contracts are supporting evidence only;
they do not satisfy this gate. This first priority is now accepted; work
continues against the wider OSS objective.

OpenCode v2 is one reference for selecting useful coding capabilities. It is
not an API, protocol, compatibility, or full-parity target. xMind implements
the selected capabilities independently in its native C++ backend and exposes
them through xMind's own HTTP, CLI, webpage, and VS Code contracts.

2026-10-16 native Release verification: the complete `ALL_BUILD` target
completed successfully and CTest passed **146/146** contracts in 868.35 seconds.
The long local-profile lifecycle and local-view contracts both passed. This
validates the current suite and does not claim all planned xMind capabilities
are complete. [Build and test evidence](evidence/native-release-suite-20261016.json),
[CTest output](evidence/native-full-release-ctest-20261016.log).

2026-10-16 MCP additional-input safety slice: native decoding now recognizes a
valid modern `input_required` tool result and preserves its bounded embedded
requests and opaque state. The journal records the operation as unresolved when
no continuation owner exists, and a real peer contract verifies that the
external tool is not replayed or marked successful. The focused Release wire,
stdio, HTTP and effect contracts pass **4/4**. User input, per-round retry,
durable resume and legacy request handling remain open. [MCP implementation
boundary](native-mcp.md).
The same 25-contract MCP/agent/graph subset passed on the subsequent full run;
one intervening repeat hit a proposal timeout in the broad MCP effect fixture.
The isolated effect test and full subset passed afterward. [Evidence and the
repeat diagnostic](evidence/native-mcp-input-required-20261016.json).

Current progress: separate live coding tasks now pass through both clients.
The browser used its saved OpenAI profile to review and apply a focused
Model Zoo accessibility change, ran the frontend production build, and passed
the backend's nine API tests under xlang3. VS Code used its saved Claude
profile to review and apply a focused frontend improvement and ran the same
production build successfully. The [browser record](evidence/testproj-xmind-browser-coding-20261015.json)
and [VS Code record](evidence/testproj-vscode-live-coding-20261015.json)
capture those results. The first coding loop is therefore demonstrated in
each client. Native restart persistence passes its shared-backend contract, and
the browser/VS Code profile check confirms the same selected session identity
and 31 saved messages. At the time of these earlier records, the shared transcript still awaited live
VS Code confirmation. The 2026-10-16 live acceptance below has since closed
that check.

2026-10-16 xAI native model policy: xMind's `xai.responses` workspace
catalogue now admits the exact documented text tool models `grok-4.3`,
`grok-4.5`, `grok-4.6` and `grok-4.7`. Reasoning choices are scoped per model:
Grok 4.3 allows `none` through `high`, 4.5 allows `low` through `high`, and
4.6/4.7 also allow `xhigh`. Unknown and non-text catalogue entries remain
filtered. The Release request-policy and actual-server profile-runtime
contracts pass **2/2**. The runtime contract discovers the models, runs a
streamed Grok 4.6 `read_file` call against a real workspace file, and reopens
the embedded xlang3/SQLite profile. Provider HTTP and responses are synthetic;
this does not claim live xAI account acceptance. [Contract output](evidence/native-xai-reviewed-model-policy-ctest-20261016.log),
[source and binary evidence](evidence/native-xai-reviewed-model-policy-20261016.json).

2026-10-16 xAI reasoning continuation: the native provider runtime contract now
streams a completed opaque reasoning item together with a Grok 4.6 tool call,
executes the actual workspace read, and asserts that the next Responses request
replays the exact completed reasoning item before the tool output. This covers
the xAI-specific encrypted reasoning requirement in the real C++ parser and
serializer. The focused Release contract passes **1/1**; the HTTP/SSE peer and
opaque ciphertext are synthetic, so live xAI acceptance remains open. [Contract
output](evidence/native-xai-reasoning-replay-ctest-20261016.log), [test
evidence](evidence/native-xai-reasoning-replay-20261016.json).

2026-10-16 live cross-client continuity and VS Code coding acceptance: VS Code
and the external webpage attached to the local TestProj profile and used its
saved OpenAI configuration. In VS Code, xMind proposed a focused
accessibility/usability change to `model-zoo/frontend/src/components/ModelCard.vue`;
the user reviewed and approved it, and `npm run build` completed successfully
(37 Vite modules, exit 0). The webpage continued the shared conversation with
a read-only follow-up, returned provider/model and token/timing metrics, and the
refreshed conversation was visible in VS Code. No provider key or server token
was requested. TestProj has no Git repository, so a full source diff against a
tracked baseline was unavailable. [Sanitized evidence](evidence/xmind-cross-client-live-coding-20261016.json).

2026-10-16 live webpage coding acceptance: from the browser UI on TestProj,
xMind proposed a one-file Model Zoo hero refinement, showed the diff, and
waited for approval. After review, the page's **Allow patch edit** control
approved the change to `model-zoo/frontend/src/views/HomeView.vue`; the same
webpage run then executed `npm run build` through the native process tool.
Vite 6.4.4 transformed 37 modules and exited 0. The saved OpenAI profile and
`gpt-5.6-sol` model were reused without asking for a provider key or server
token. Vite served the updated frontend at `127.0.0.1:4174`; the TestProj API
on port 8000 was unavailable during visual review, so its seeded stats and
catalog results could not load. This proves the webpage's reviewed edit and
build loop, while full page data rendering still depends on the TestProj API.
[Sanitized acceptance evidence](evidence/xmind-webpage-live-coding-20261016.json)
and [run screenshot](evidence/xmind-webpage-live-coding-20261016.png).

The live VS Code acceptance also exposed a Windows path normalization bug:
VS Code can report the development extension drive letter in lowercase while
`realpath()` reports it uppercase. xMind now normalizes the volume letter
without changing case-sensitive directory components, and the regression test
covers both properties. The complete VS Code extension suite passes **386/386**.

2026-10-16 unified CLI live profile check: the single `xmind` binary attached to
the existing TestProj local profile, discovered its saved Claude model without
re-entering credentials, and completed a read-only framework inspection using
real workspace `list_files` and `read_file` calls. The run completed and its
persisted transcript records the provider/model and tool results. This confirms
the console uses the same managed backend and workspace; the CLI coding-edit flow was a separate acceptance item at that checkpoint; the live CLI coding
acceptance below has since closed it. [Evidence](evidence/xmind-cli-live-framework-inspection-20261016.json).

2026-10-16 unified CLI live coding acceptance: the interactive `xmind` console
attached to the existing TestProj local profile and reused its saved OpenAI
Responses configuration (`gpt-5.6-sol`). The model inspected the frontend,
proposed a one-line accessible name for the API reference link, and xMind
presented the native `replace_file` operation with its exact diff. The CLI
approved that exact operation through `/allow`; the journal recorded
`operation.succeeded` with actor `local-owner`. The resulting `App.vue` was
independently read from disk and its SHA-256 matched the native receipt. A
separate approved `run_process` call ran only `npm run build` from
`model-zoo/frontend`; it exited 0, transformed 37 Vite modules, and completed in
613 ms. The sample-data disclosure remained intact. [Sanitized CLI coding evidence](evidence/xmind-cli-live-coding-20261016.json).

2026-10-16 terminal CLI rendering follow-up: ordinary interactive chat now
renders assistant text, concise tool activity, actual provider token counts,
and terminal run state without dumping backend event JSON. File changes retain
their bounded diff and exact `/allow` decision; process proposals show the
profile, working directory, command, timeout and environment variable names.
TTY sessions select human output automatically. Piped consumers retain NDJSON
and can explicitly select either format with `XMIND_CLI_OUTPUT=human|json`.
Release builds of `xmind_cli` and unified `xmind` passed, and
`native_agent_patch_http_contract` passed with synthetic provider data,
including streamed text/usage, a real native approval and file receipt in the
human renderer. The rebuilt binary then attached to the existing TestProj
profile in a real terminal and completed a saved-provider read-only run: it
streamed progress for two `find_files` and two `read_file` calls, displayed
actual input/output/cache counters, and returned the frontend build command.
The run's operation list was empty and no provider key was requested.
[Evidence](evidence/xmind-cli-human-output-20261016.json).

2026-10-16 shared session catalogue check: read-only `sessions` requests through
the managed CLI profile and the browser's authenticated local view adapter
returned the same 24 TestProj session IDs. This confirms the adapter forwards
to the profile's native session catalogue; it does not prove the selected
session is rendered inside VS Code. [Evidence](evidence/cli-view-shared-session-catalogue-20261016.json).

On 2026-10-16, a fresh local webpage run against TestProj also loaded the four
saved providers from `.config/providers.yaml`, discovered 20 OpenAI models,
completed a real coding turn, displayed provider token/timing metrics, showed
the native patch for approval, applied it after approval, and built the Vue
frontend successfully. The run's first model response arrived in 27.48 seconds
and completed in 46.67 seconds; the follow-up edit and validation completed
with 2.90 seconds to first token. See the [live browser record](evidence/testproj-webpage-coding-live-20261016.json).
The native restart and history-persistence contract passes; the TestProj browser
and VS Code state also identify the same saved conversation. At that checkpoint, direct visual confirmation and a coding turn from the shared conversation remained open. The live cross-client acceptance above has since closed both checks.
The strengthened `native_local_view_contract` now passes **1/1**: its real
native SQLite server, production browser adapter and VS Code `BackendClient`
share a session, restart the backend, then each read the exact same saved
history. This proves the shared API and persistence layer. The live TestProj
profile check confirms that the browser selection matches VS Code's persisted
session ID and saved history; visual confirmation inside the native VS Code
pane remains open. The browser now also
opens a real xterm.js terminal against the same native session-bound PTY as
VS Code, with authenticated input/output/resize/close routes and cleanup on
workspace or conversation changes. The browser page and PTY unit contracts
pass; real browser PTY acceptance remains outstanding. The latest client suites
pass **385/385** in VS Code and **133/133** in the browser; rebuilt native patch, patch-tool, edit-executor,
agent HTTP and unified HTTP contracts pass **5/5**. See the
[cross-client restart evidence](evidence/cross-client-session-persistence-20261016.json).
The current source also passes the full extension and browser client suites
385/385 and 133/133 respectively ([current-checkout evidence](evidence/client-suites-current-20261016.json)); these client contracts complement, but do not replace, the open rendered VS Code pane acceptance.
VS Code now also accepts the machine-level `agentflow.profileRoot` setting so it can use the same explicit local profile root as the webpage launcher. The optional `agentflow.openOnStartup` setting opens the xMind view after extension activation when enabled; it defaults off. The VS Code client suite now passes **385/385**, including CSP-protected startup and safe connection-failure guidance, stale-selection recovery, workspace-profile isolation and a saved-session coding turn routed to the selected session. Those contracts use a fixture backend and do not prove live pane rendering. In the TestProj acceptance profile, the browser visibly rendered a saved coding conversation. The VS Code extension state selected the same conversation and workspace, and read-only inspection found its 31 saved messages in the shared SQLite profile. The VS Code host created the xMind webview; direct visual inspection of the native VS Code pane and a live same-conversation coding turn remain open. [Sanitized evidence](evidence/cross-client-shared-profile-20261016.json).

The current VS Code package is **0.1.12**. Its VSIX passed the native runtime manifest check and installed in the local VS Code profile. The extension also launched a native TestProj view and attached it to the same persistent profile used by the live webpage coding run. This confirms packaging, installation and shared-backend attachment; a coding turn from the rendered VS Code pane is still unverified. The default VS Code profile continues to report a webview service-worker failure, while a fresh profile rendered the built-in Markdown preview; xMind rendering in that fresh profile remains unverified. See the [shared backend evidence](evidence/vscode-shared-backend-20261015.json) and [clean-profile webview diagnostic](evidence/vscode-webview-clean-profile-20261015.json).

## CLI attachment to native queued runs — 2026-10-15

When `/v1/health` advertises `session_inbox_dispatch`, the interactive console
now waits on its saved inbox row, validates the claimed run belongs to the same
root session, and attaches through the ordinary run observer. It no longer
competes with the server by posting a second `/v1/runs` admission. While
dispatch is pending, `/cancel` removes the oldest queued entry, `/exit` detaches,
and ordinary prompt text is appended to the same durable inbox. Older servers
that omit the capability retain the existing console-side admission fallback.
The rebuilt Release `native_agent_http_contract` and
`native_http_cli_contract` pass **2/2**; the actual-server CLI scenario verifies
one admitted run (its two expected provider requests are the tool call and
follow-up) and exactly one saved user message. Provider replies are synthetic.

## Reusable xlang3 tool workers — 2026-10-15

Registered trusted xlang3 tools now reuse a local process across calls for the
same module/function/runtime roots. xMind serializes concurrent invocations to
each worker, caps the process registry at 16 sessions, retires an idle session
when capacity is needed, and retires workers after cancellation or an IPC/result
failure. It never retries a dispatched call after transport failure. The
Release worker and actual AgentRunner contracts pass **2/2**, including
worker-local state reuse, serialized concurrent calls, cancellation, and
persisted model continuation. The complete Release CTest suite passes
**145/145** in **378.78 seconds**. A prior full run had one transient local-view
profile-record publish failure; the contract passed alone and with the slow
local-profile contract in parallel, then passed in the final full run. Durable
attempt leases/reconciliation, worker authentication beyond spawned PID,
remote scheduling, and OS sandboxing remain open. [Design and limits](native-ipc-worker-design.md),
[final full test output](evidence/native-reusable-workers-ctest-retry-20261015.log).

## Optional OpenRouter model provider — 2026-10-15

xMind's native model gateway now exposes OpenRouter as an optional provider
route, alongside direct provider integrations. Its model catalogue is discovered
from OpenRouter's `/models` endpoint with the tools-capability filter, then
bound to the saved profile and the existing provider/model selector. This is a
gateway adapter, not a required account or replacement for direct OpenAI,
Anthropic, Gemini, DeepSeek, xAI or Mistral profiles. The Release build passed
the complete native CTest suite **145/145** after rebuilding all native targets;
the browser/VS Code client suite passed **495/495**. All provider credentials
and model responses in the focused route tests are synthetic; no live
OpenRouter account or inference was tested. LiteLLM's provider catalogue is used
only as a discovery reference; xMind's own provider adapters and model behavior
are implemented and validated independently. [Validation record](VALIDATION_STATUS.md#optional-openrouter-provider-and-clean-release-validation--2026-10-15).
[Release test summary](evidence/native-openrouter-release-ctest-20261015.json).

## Restore approved coding changes — 2026-10-15

The coding agent can now reverse one successful `edit_file`, `create_file`, or
`apply_patch` operation by its saved operation ID. The native restore executor
reconstructs only the inverse of that durable file change, verifies the
workspace membership and folder revision, checks saved before/after hashes and
the live file identity/content, then submits the inverse through the existing
`patch_file` approval journal. Update, add, delete, and move reversals are
covered with real files and SQLite. The focused Release contracts pass **8/8**,
including native agent HTTP tool-catalogue, create/edit/patch execution, stale
restore rejection, and restore approval. There is not yet a direct Undo button
in the browser or VS Code; the agent invokes the restore tool from conversation
history. The browser and VS Code now show **Prepare restore request** on an
eligible completed file change. The control re-fetches and validates the saved
operation, then fills the composer with a request naming its operation ID; it
does not submit the request or approve the resulting inverse patch. Client
validation covers successful edit/create operations and patch update/add/delete/
move receipts. The VS Code suite passes **365/365**, including extension-host
revalidation, and the browser suite
passes **119/119**. [Restore executor contract](../Native/tests/restore_executor_contract.cpp).

## Action-aware uncertain file recovery inspection — 2026-10-15

Native inspection now classifies uncertain replacement and patch add/update/
delete/move operations against their approved before and after state. Move
inspection fingerprints both source and destination; optional paths are
observed through verified workspace-root and ancestor handles. Responses expose
path fingerprints and absence only, never file contents. Inspection is read-only
and leaves the durable operation uncertain and the workspace quarantined.
Release CTest passes the native edit executor and approved-edit HTTP/CLI
contracts **2/2**. Browser controller tests pass **63/63**, and the VS Code
client suite passes **362/362**, including patch-add inspection validation.
Direct UI Undo/redo, attributed reconciliation and post-write process-kill
recovery remain open. Agent-invoked restore of a saved approved file operation
is now covered in [the restore milestone](#restore-approved-coding-changes--2026-10-15).
[Native recovery contract](../Native/tests/edit_executor_contract.cpp).

## Saved-provider live browser smoke across four gateways — 2026-10-14

The local xMind browser reused its encrypted saved credentials and completed
short no-tool responses through OpenAI Responses (`gpt-5.6-luna`), Anthropic
Messages (`claude-haiku-4-5-20251001`), Gemini GenerateContent
(`models/gemini-3.8-flash`) and DeepSeek Chat Completions (`deepseek-flash`).
The sidebar showed each admitted profile/model, provider token counts and
backend timing. First-token latency ranged from 0.54 to 2.53 seconds in these
four requests. No provider key was entered in the page, and the smoke prompts
did not invoke workspace tools. A clean conversation was required when changing
from existing OpenAI-specific tool history to Claude; the original history was
preserved. Gemini's saved-key catalogue returned 11 models, but the selected
`models/gemini-2.5-flash` returned HTTP 404 while `models/gemini-3.8-flash`
completed. The single-model 404 cause remains unverified. The earlier uncertain
preview operation was not retried. This is live saved-key/provider wire
evidence, not broad per-model, tool-call, coding or installed-package parity.
[Sanitized run records](evidence/provider-live-ui-smoke-20261014.json).

The 2.5 Flash HTTP 404 exposed an observability gap: Google’s structured
`NOT_FOUND` status was discarded even though the HTTP status survived. The
native transport now preserves only a strict allowlist of provider status
identifiers, and the shared browser/VS Code failure card explains that the
selected model or endpoint may be unavailable to the saved credential without
showing the provider’s error body. A synthetic Gemini socket contract passes
the structured-404 boundary, the VS Code renderer suite passes **89/89**, and
the Release `xmind` executable links successfully. Google documents that
Gemini 2.5 Flash has no announced shutdown but access is restricted to users
who actively used 2.5; this supports an account-access explanation but does
not establish the cause of this specific 404. [Google Gemini deprecations](https://ai.google.dev/gemini-api/docs/deprecations/).

## Native workspace Markdown slash commands — 2026-10-14

xMind now discovers bounded Markdown prompt commands under
`.agents/commands/`, expands quoted positional arguments without reinterpreting
user text, and exposes authenticated workspace-catalogue and session-execution
routes. The browser and VS Code composers resolve `/command arguments` and
submit through the existing single-agent or selected graph run path with
workspace and provider authority. The interactive CLI lists `/commands` and
dispatches slash IDs through the same endpoint with exact workspace authority.
Command files are prompt text only; xMind
does not execute shell, Python or plugin code from them. Release CTest passes
the native parser, live server/browser-adapter and CLI dispatch contracts **3/3**;
the focused browser/VS Code client and controller suite passes **206/206**.
Plugin/dynamic command sources, command autocomplete, and shell interpolation
remain open. [Command contract](native-commands.md).

## Reject direct CPython process launches — 2026-10-14

The native process executor now rejects direct CPython executable profiles and
common `python`, versioned `python3.x`, `pythonw`, and `py` launcher tokens in
the registered PowerShell, cmd, bash, and sh profiles before an operation is
proposed or dispatched. The diagnostic directs Python-compatible commands to
the paired xlang3 profile. Its actual process contract verifies rejected
commands leave no operation record and that xlang3 package imports and normal
approved shell commands still work. The focused Release CTest passes **1/1**.
This is a policy guard for direct launcher forms, not an OS sandbox against
arbitrary shell obfuscation. [Focused validation record](evidence/native-cpython-launch-guard-ctest-20261014.json).

## Profile-preserving browser runtime upgrade — 2026-10-14

The managed TestProj profile upgraded in place to the pushed xMind `f82e79c`
runtime paired with xlang3 `f6bbc561`; the backend executable hash matches the
staged artifact, and the existing profile/database remained selected. The
owner handoff took about 72 seconds, exceeding the browser launcher’s old
60-second wait and leaving the native view ready after its adapter had timed
out. The launcher now waits 120 seconds. A browser adapter attached to that
same native view at `127.0.0.1:60417`; the page and all seven JS/CSS assets
returned HTTP 200. Its local session connected and disconnected, `/v1/health`
reported `ok` with agent execution enabled, TestProj remained selected, and
the provider catalogue returned four saved profiles without keys. This is
local HTTP/client-route acceptance; external-browser rendering and a new model
write/build run are still open. [Sanitized evidence](evidence/browser-profile-upgrade-live-20261014.json).

## Live saved-provider model discovery — 2026-10-14

Through the active browser view on the same TestProj backend, xMind used the
four saved provider profiles to fetch model catalogues without sending keys to
the browser or changing provider configuration. Anthropic returned 13 models,
Gemini 11, DeepSeek 2, and OpenAI 20. The active OpenAI model endpoint itself
still returns only the selected model; the browser's existing background
discovery fills the sidebar list after workspace/session initialization. A
regression test verifies that this discovery is asynchronous and populates all
returned model choices. The combined browser and VS Code client suites pass
**456/456**. This validates current saved-key discovery and model-list wiring,
not a live model-generated edit/build or visual browser review.
[Sanitized evidence](evidence/provider-profile-discovery-live-20261014.json).

## Current-runtime TestProj coding and build — 2026-10-14

The pushed xMind `9261b67` runtime paired with xlang3 `f6bbc561` completed a
real saved-OpenAI-profile run in the native TestProj backend using
`gpt-5.6-luna`. After review, the run applied one CSS declaration to
`model-zoo/frontend/src/style.css`, adding a faint amber top gradient to model
cards. Its native PowerShell process tool ran `npm.cmd run build` in the
frontend folder; Vite 6.4.4 transformed 37 modules, exited 0, and completed in
597 ms. The emitted production CSS contains the new gradient, and run history
records 33,519 input and 212 output tokens. This exercises the current
provider-bound model/tool/edit/build path. External visual page review remains
unverified: an attempted second preview used a 1,000 ms process timeout and
became uncertain on port 4173; its quarantine was preserved without replay or
reset. The existing TestProj Vite dev server on port 4174 remains live, returns
HTTP 200 for the Model Zoo page, and serves the updated stylesheet containing
the gradient. This confirms the updated source is reachable, not that its
rendered appearance was visually reviewed.
[Sanitized evidence](evidence/testproj-current-runtime-coding-20261014.json).

## Background process timeout and server guidance — 2026-10-14

The TestProj preview attempt exposed a tool-guidance gap: its generated
`run_process` call set `timeout_ms=1000`, which retired Vite before its ready
URL appeared. xMind correctly retained the resulting process operation as
uncertain, and that workspace/profile quarantine remains untouched. The
AgentRunner joins background jobs before it completes, so this mode cannot
leave a persistent server behind. The process tool now states the timeout,
join, and persistent-server semantics explicitly and routes servers that must
outlive a run to the session Terminal UI. The Release native agent/process HTTP
contract passes **1/1** with assertions for this guidance. Persistent
model-managed service lifecycle is still a product gap.
[Validation evidence](evidence/background-process-tool-guidance-20261014.json).

## Persistent process lifecycle for local development — 2026-10-14

The earlier joined-background behavior is replaced by explicit persistent
services for ordinary session agents. A start uses
`background=true,persistent=true`; the assistant can finish while the native
backend continues to own the process. A later same-session run can inspect its
startup output and stop it through `stop_process`. Normal workspace commands
remain available while the service runs. SQLite schema 28 exempts only these
explicit services from the executing-effect workspace lock; other effects
retain their existing resource constraints.

The Release process contract verifies service start, cross-run output access,
workspace command execution during service lifetime, session/workspace stop
authorization and observed process-tree exit. The HTTP tool contract verifies
the new tool schema. Services still end at their trusted profile timeout or
backend shutdown, and do not resume after restart. A live TestProj preview
started by the model and rendered browser/VS Code review remain open.

## Nested human-wait wake handoff — 2026-10-14

A focused Release run exposed a timing race: a nested human answer could be
accepted while the root worker was still unwinding the durable pause. The
answer then raced the worker's pause validation and the root remained paused.
`plan_input` now waits for that physical worker to reach its waiting phase
before validating and committing the answer; terminal/faulted ownership wakes
the waiter without admitting input. The complete dynamic-plan engine contract,
including nested answer/resume and SQLite close/reopen recovery, passed five
consecutive Release runs. Simultaneous nested human waits across sibling
branches remain unsupported.

## MCP SDK interoperability recheck and OpenCode inventory — 2026-10-14

After the broad native suite reported timeouts for the two SDK-era contracts,
the MCP graph HTTP, graph subscription, AgentRunner HTTP, AgentRunner
subscription, official modern SDK and official legacy SDK contracts were run
together in Release mode. All **6/6 passed in 13.93 seconds**. Each SDK case
uses the actual native server/admin/CLI and official peer package with
synthetic inference; this scoped pass does not establish completion of the
full native suite or all MCP behavior.

The pinned OpenCode v2.0.26 inventory was regenerated after the recent native
process and persistence changes. Its pin remains commit
`9b4ec5714d481559990db0a816d5dec19541a814`: **141 operations**, **36 reviewed
native source candidates**, **105 unmapped**, and **zero operation-level parity
verified**. Candidate source hashes are refreshed in
[`OPENCODE_API_INVENTORY.json`](OPENCODE_API_INVENTORY.json); full coding and
API parity remain open.

## Managed unified CLI workspace acceptance — 2026-10-14

The profile-managed unified `xmind.exe` connected to the existing TestProj
backend with `started=false`, then `workspace info` returned the registered
TestProj root, workspace/folder identity and revision 1. The packaged runtime's
xMind and xlang3 DLL hashes match the previously verified staged artifact.
This confirms the local CLI uses the same managed profile and workspace as the
browser without launching a duplicate owner. Running the unstaged build-tree
binary is not equivalent because it lacks adjacent packaged xlang3 modules.
[Sanitized evidence](evidence/managed-cli-workspace-testproj-20261014.json).

## Fresh full native Release gate — 2026-10-14

The current native source rebuilt in the dedicated `build/native-verify` tree
and passed **144/144 CTest contracts** in 211.38 seconds, with no failures or
skips. This includes the automatic A2A stream reconnect and missing-cursor
uncertainty cases, dynamic-plan MCP/process coverage, graph, CLI and embedded
xlang3 SQLite. The complete CTest output is recorded in
[validation status](VALIDATION_STATUS.md#full-native-release-gate--2026-10-14).
The exact command and result metadata are also available in the
[evidence record](evidence/native-full-release-ctest-20261014.json).
The shared browser and VS Code client contracts pass **452/452** on the same
source checkpoint; see the [client evidence record](evidence/browser-vscode-contracts-20261014.json).
Synthetic peers and providers remain contract fixtures, not live interoperability
or installed-client evidence.

## MCP resources and prompts as dependency-plan nodes (2026-10-14)

Dynamic plans can now select registered MCP resource and prompt aliases from
the sealed coding-preset catalog. Each read executes in its own model-free
child through a fresh native peer connection, exact catalog revalidation,
controller approval and the durable operation journal. A native synthetic
subprocess contract reads a resource, then retrieves a dependent prompt and
checks both actual replies and that no effect tool ran. The focused Release
contracts pass **6/6**. No xlang3 source change was needed. The rebuilt full
native suite passed 143/144; the single Rust Analyzer call-site-definition
failure passed its immediate isolated rerun with the fixture check (2/2). See
[validation status](VALIDATION_STATUS.md).

## Fresh native Release validation (2026-10-14)

A clean Visual Studio 18 Release build in `build/native-verify` passed all
**144 native CTest contracts**, including the optional Rust Analyzer and
multi-folder language-server tests. The new tree avoided replacing the xMind
binary still serving the user's TestProj workspace. Four contract mismatches
from older in-place binaries disappeared after rebuilding; the local-view
contract also passed on retry and in the complete run. The provider CLI
contract was updated to expect the native `mistral.chat` route already
advertised by the product. This checkpoint verifies the complete local native
Release suite, not hosted CI or live-provider/editor acceptance.

## Dynamic plan tool nodes in the shared Browser/VS Code view (2026-10-14)

The shared client accepts the six native deterministic read-only workspace
aliases as dynamic-plan `tool` nodes and validates their preserved argument
JSON. It validates model-free owned-child claims without assigning an agent
preset or fabricated provider metadata. The shared renderer labels the tool and
its arguments, then displays the actual child result without invented model
metrics. The full VS Code extension suite passes **343/343**, including client
boundary and rendered-view coverage. This covers the shared renderer used by
the browser and VS Code; external-browser and installed-extension acceptance
remain open. See [validation status](VALIDATION_STATUS.md).

## Bounded unified diffs for console approvals

The console renders file proposals as bounded unified diffs with before/after
paths, hunk counts and unchanged context. TTY users see a readable preview;
JSON consumers receive the exact `operation_review` record. Lines and paths
remain JSON-escaped so file content cannot inject terminal control sequences.
Each side is limited to 40 lines and 8 KiB of encoded preview text; truncation
is explicit, and the durable operation ID remains the approval authority.
Process proposals also show the exact command and working directory. Native
edit and patch HTTP contracts verify updates, additions, moves and deletions.
Full-screen TUI parity remains open.

## Durable outbound A2A SSE cursor checkpoint (2026-10-14)

The native A2A client now preserves peer-issued SSE `id` values from both the
initial streamed send and later task resubscriptions. xMind commits each
non-empty cursor alongside its event in the owning run's SQLite journal, then
sends the latest committed cursor as `Last-Event-ID` on the next manual
resubscription. Cursor values are validated before entering the trusted native
HTTP transport. After backend reopen, the resubscription contract resumes an
interrupted task from its SQLite cursor and appends the new observation to the
terminal run journal without replaying the original send. Terminal-run appends
are limited to matching events for a succeeded or uncertain A2A operation, and
the contract rejects a different remote task ID. The native client, registry
and repository Release contracts pass 3/3. This is best-effort peer
resumption; A2A does not require a peer to issue or honor event IDs, and this
does not establish automatic recovery or exactly-once delivery. No xlang3
change was needed. See [validation evidence](evidence/native-a2a-sse-restart-ctest-20261014.log)
and [A2A limitations](native-a2a-client.md).

## Model-free run admission and managed local view verification (2026-10-13)

The console no longer sends an empty provider-profile binding when no provider
is selected, so model-free single-agent and graph runs can be admitted. The
native API rejects a supplied empty profile identity before creating a run,
and CLI admission errors now include the backend's actionable detail. Fresh
Visual Studio 18 Release binaries passed the graph-service, unified graph,
unified program, console workspace, provider-profile runtime and provider-profile
CLI contracts. The managed local-profile and browser-view contracts also passed
with a Unicode workspace, as did the backend handoff contract. The xlang3
SQLite contract passed; no xlang3 source change was needed. These tests exercise
native browser controllers and APIs, not rendered-page or installed-IDE
acceptance.

The context-recovery contract also now compares every message field and payload
byte-for-byte across restart. Its former 22-message expectation counted one
message that the fixture never creates; the setup produces 21, and the rebuilt
Release contract passes.

## Durable CLI conversation model selection (2026-10-13)

The console chat now reads the selected model for an opened session from the
native API and restores it only when its provider profile and route are
active. `/model ID` persists a selection with the provider and session
revisions; `/model` clears the override so run admission resolves the provider's
current default. A nullable SQLite selection with a monotonic revision
preserves compare-and-swap safety, and the v21→v22 migration keeps existing
provider-bound choices. The command can start a conversation when none is
selected. The actual compiled CLI acceptance test selects a model, verifies its
native write, restarts the SQLite runtime and confirms restoration, then clears
the choice and verifies the next inference uses the profile default. The
focused Release CTests pass. No xlang3 source change was needed.

## Portable local session archives (2026-10-13)

Added a versioned xMind conversation archive through the native HTTP backend
and console client. Version 2 carries the selected agent snapshot, and version
1 archives remain importable. `export-session` writes without overwriting an
existing file; `import-session` creates a fresh session and new local message
sequence IDs. Export refuses active runs. The importer validates message roles
and limits, and stores session, agent selection and transcript atomically
through the xlang3-backed SQLite repository. The focused HTTP/CLI contract
passed, including both archive versions and an agent-settings round trip. No
xlang3 changes were required; the change uses existing SQLite capabilities.

## Durable prompt cancellation in Browser and VS Code — 2026-10-13

Queued prompts can be canceled from either UI. SQLite keeps canceled IDs
reserved so retries are idempotent and an old ID cannot accidentally create a
new prompt. The v19→v20 schema migration preserves pending and claimed input,
including claimed run links. Release native repository and HTTP contracts pass
2/2; browser and VS Code contracts pass 98/98. This closes an xMind inbox
behavior gap and required no general xlang3 optimization.

## Ordered cross-folder `apply_patch` for ordinary agents — 2026-10-13

An ordinary AgentRunner can now accept one ordered `patches` array spanning
registered workspace folders captured at run admission. The backend validates
every folder and prepares every patch group before effects begin, then applies
groups in order with an independent durable approval for each file. Its
aggregate receipt records requested and processed folders, per-folder outcomes,
file counts and partial stops. A denied second-folder edit leaves an already
approved first-folder edit intact and reports `partial`; it never describes the
batch as atomic. Graph patch nodes remain single-folder. The native
AgentRunner/provider-peer contract passes both successful and denied cross-root
flows with real filesystem effects. This feature used the existing xMind
runtime interfaces and required no xlang3 change. Rendered browser/VS Code
acceptance and complete coding parity remain open.

## Bounded recent conversation history in Browser and VS Code — 2026-10-13

Opening a long conversation now fetches and renders at most the 40 newest
messages in both clients. The existing “Load older messages” paging path still
reaches the complete durable history and retains its chronological ordering.
This reduces the initial transcript DOM work; it does not claim model inference
or general browser latency improvements. The VS Code suite passes **333/333**
and the browser suite passes **106/106**, with no failures or skips.

## Live configured-provider CLI patch approval — 2026-10-13

A live Claude Sonnet 4.6 CLI run revealed that xMind's model-facing patch
description did not distinguish its custom `@@` hunk markers from numbered
unified-diff headers. The tool guidance and contract now state the accepted
syntax explicitly. With the rebuilt native CLI, a live `patch_file` proposal
showed both exact replacements in the before/after preview; after approval,
only the intended workspace file changed and the run completed. The focused
native patch-tool contract passed. No xlang3 code change was needed. See the
[sanitized evidence](evidence/native-cli-live-patch-preview-20261013.json).

## Live configured-provider CLI edit approval — 2026-10-13

The native CLI discovered and selected Claude Sonnet 4.6 from the existing
local provider configuration without another key prompt. In an isolated
temporary workspace, one live `replace_file` proposal showed its before/after
preview before approval. The approved operation changed only `message.txt`
from `before` to `after`; the run completed and the temporary server/workspace
were removed. Live-model CLI `patch_file` acceptance remains open. See the
[sanitized evidence](evidence/native-cli-live-edit-preview-20261013.json).

## Human-readable CLI edit and patch approval previews — 2026-10-13

At the time, the interactive console emitted JSON `operation_review` records
for machine clients and printed readable per-file previews for `create_file`,
`replace_file` and `patch_file`. The 2026-10-16 terminal renderer milestone
above now selects the human form for TTY sessions while preserving JSON for
machine clients. The durable operation ID remains the only approval target.

## Batched coding-edit guidance after live TestProj run — 2026-10-13

The coding tools now direct the model to use one `apply_patch` proposal for
related edits and multiple hunks, and reserve `edit_file` for a focused literal
replacement. Per-file controller approval, exact-context matching, and partial
batch reporting are unchanged. This addresses the live TestProj run where many
small sequential edits exhausted the run before its successful Vite build could
be reported; the focused native Release `native_patch_tool_contract` passes.
A completed coding run in the external xMind browser then made one approved
`patch_file` update to `TestProj/model-zoo/frontend/src/style.css` and ran
`npm.cmd run build`: exit 0, Vite 6.4.4, 36 modules transformed, built in
700 ms, with no stderr. This verifies the real page's write-and-build path. It
does not by itself establish that all longer coding requests avoid timeouts.

## Post-fix native provider and MCP regression gate — 2026-10-13

The exact pushed xMind source `ccaeb07` passes the full **144/144 native
Release CTest suite in 676.83 seconds**, with no failures or skips. This includes
Responses-history wire preservation, Claude/Gemini/DeepSeek cross-wire history
checks, modern and legacy official MCP SDK tool peers, native MCP resources,
graph/MCP/A2A, CLI, and managed local-profile/view lifecycle tests. The first
run's three failures and the four passing focused retests are preserved beside
the clean full rerun. Rebuilding also exercised the separate configured
`build/native` output tree and left the active backend build directory alone.
[Full CTest output](evidence/native-144-ctest-postfix-20261013.log),
[failure/fix history](evidence/native-144-ctest-20261013.log).

The source-generated OpenCode V2.0.26 inventory was refreshed against its
pinned commit: **141 operations, 36 partial native source candidates, 105
unmapped, and zero verified operation-level parity**. The inventory generator
was rerun on 2026-10-14 and confirmed all mapped source anchors and hashes.
OpenCode and LiteLLM remain references; no foreign implementation was added.
The broader coding, CLI/editor and endpoint parity gaps remain open in
[PARITY.md](PARITY.md).

## CMake-paired xlang3 selection for the TestProj development host — 2026-10-13

The VS Code launcher now reads `AGENTFLOW_XLANG3_RUNTIME_DIR` from the same
`build/native/CMakeCache.txt` used for xMind when no explicit runtime override
is given. It keeps the DLL pair check and snapshots the selected runtime before
launching the editor, avoiding an accidental mismatch with a stale sibling
`xlang3/build/Release` directory. The four-case PowerShell selection contract
passes. A fresh VS Code development host opened TestProj; its snapshotted
`xlang3.exe` and runtime DLL exactly matched the configured compatibility
branch, and an xlang3 interpreter probe succeeded. No provider inference was
performed because the fresh profile had no active model; actual AgentRunner
pytest acceptance through the selected profile remains open.
[Runtime-pair evidence](evidence/start-ui-xlang3-runtime-pair-20261013.json),
[selection test](../Tools/tests/xlang3-runtime.test.ps1).

## Backend-paired xlang3 process profile — 2026-10-13

When a Windows local backend initializes its trusted process registry, its
active process snapshot adds an `xlang3` profile if `xlang3.exe` exists next to
the configured runtime modules and a trusted PowerShell or cmd profile is
enabled. This includes managed local views that pass the repository's explicit
`--process-config`. The runtime profile inherits the shell's approval and
timeout policy. Its executable identity is recalculated each backend startup
rather than persisted across runtime upgrades. Empty and custom-only process
lists remain opt-outs; the user-owned config file is not rewritten. The coding
agent is told to use this profile for Python-compatible scripts, tests and
package commands instead of machine PATH. The native AgentRunner contract
starts from an explicit shell-only process config, executes an xlang3 script,
checks saved config preservation, and verifies that an explicit empty list
stays empty. The four targeted Release contracts passed, including the
packaged managed local-view contract. Live TestProj pytest through a newly
initialized xMind profile and installed VS Code UI acceptance remain open.
[Native contract output](evidence/native-xlang3-default-process-20261013.log).

## Exploratory OpenCode-shaped MCP resource location route — removed 2026-10-15

At that point, the native HTTP API exposed `GET /v1/mcp/resource?location[directory]=...`
with the pinned OpenCode `location` plus `data.resources`/`data.templates`
shape, while retaining xMind's existing `/v1/mcp/resources` UI catalog. The
native contract test verified exact public fields, path normalization, rejection
of unknown/duplicate/relative location queries, and bearer authentication. The
full browser and VS Code Node suites passed **432/432** at the time. This was an
exploratory reference experiment, not a product compatibility goal. The route,
foreign payload projection, and browser forwarding were removed on 2026-10-15;
xMind now exposes its native `/v1/mcp/resources` catalog only.
[Native contract output](evidence/native-mcp-resource-location-contract-20261013.log),
[browser/VS Code output](evidence/mcp-resource-location-browser-vscode-tests-20261013.log).

OpenCode and LiteLLM guide feature selection and behavior research only. xMind
does not target their API, wire, SDK, or client compatibility. The native MCP
catalog and revision-bound session-agent request are covered by xMind-owned
contracts.

## VS Code process validation on the xlang3 compatibility branch — 2026-10-13

The live VS Code Extension Development Host completed TestProj's xlang3-backed
backend test run through xMind's native process tool: all **9 pytest API tests
passed** in **1.83 seconds** using xlang3 branch
`fix/pytest-pathaware-hook-proxy` at `d876c281`, Pydantic 2.14.1's pure-Python
wheel and native Pydantic Core 2.50.1. The same UI run completed
`npm.cmd run build` with exit code **0** (Vite 6.4.4, 37 modules, 692 ms).
The existing unqualified `xlang3.exe` process lookup resolved the older
`D:\\CantorAI\\xlang3\\build\\Release\\xlang3.exe` and could not find
pytest; using the explicit branch runtime and package paths passed. This
verifies the real VS Code extension-to-backend process path for this workspace,
but not default interpreter discovery, an installed VSIX or a release package.
No project source edits were requested during this validation. Exact commands,
run IDs and limits are in
[the acceptance record](evidence/testproj-vscode-xlang3-pytest-20261013.json).

## Native agent recovery after stale edit — 2026-10-13

When an `edit_file` proposal is rejected because the file changed, the agent
now receives instructions to reread current contents and create a fresh
proposal. The native HTTP integration contract verified that a stale proposal
has no file effect, the agent reads the actual workspace file, and only the
freshly approved exact edit is applied. Both the native runner and edit HTTP
contracts pass in Release. A live webpage run with OpenAI `gpt-4.1` then
reproduced the race in TestProj: after the file changed between proposal and
approval, the stale edit was rejected, the model reread the current bytes, and
it submitted a fresh `version=current` to `version=recovered` proposal. The
second normal approval completed the run; the temporary smoke-test file was
removed after verifying its contents. VS Code recovery remains to be tested.

## Live webpage coding pass against TestProj — 2026-10-12

The webpage xMind client completed a model-assisted catalog design task against
the selected TestProj workspace. DeepSeek Flash proposed one CSS-only patch;
the user-authorized xMind review flow applied it to
`model-zoo/frontend/src/style.css`. The managed xlang3-backed process tool ran
`npm.cmd run build` successfully (exit **0**; Vite 6.4.4, 36 modules, 700 ms,
empty stderr). The refreshed catalog rendered, task filtering returned 3
Image Classification entries, Clear all restored 12, and `bert` search
returned 2. A separate live FastAPI request returned HTTP 200 with 2 entries
and total 12. This verifies a complete bounded web coding cycle with one
approved edit, managed command execution, and independent browser/API checks;
mobile viewport and pytest acceptance remain open. See the
[sanitized run record](evidence/testproj-webpage-design-run-20261012.json).

## VS Code opened-workspace and saved-provider smoke test — 2026-10-12

The live VS Code Extension Development Host selected the existing TestProj
workspace and discovered two DeepSeek models from the xMind local profile,
without re-entering or exposing its provider key. A read-only
`deepseek-flash` run traced the Model Zoo catalog request from the Vue client
to FastAPI and returned `GET /api/models`; it completed with no file edits,
commands, or process starts. The extension displayed token and latency metrics
for model calls. This verifies a real VS Code workspace-bound inference turn,
not the outstanding write/approval/build workflow. See the
[sanitized run record](evidence/testproj-vscode-inspection-20261012.json).

## Webpage workspace creation and switching acceptance — 2026-10-12

The live Edge xMind page created a second local workspace from the existing
xlang3 folder and switched between it and TestProj through the workspace
settings selector. TestProj was restored as the active workspace and remains
selected after closing settings. This verifies webpage workspace creation and
switching for the current local profile; adding another folder and running an
agent in the new workspace remain unverified. A literal `%SystemDrive%` tree
is present in TestProj with a creation timestamp during the failed run; its
origin is unclear and it remains untouched for review.

## Managed coding-agent server launches — 2026-10-12

The native `run_process` tool contract now directs coding agents to start
long-lived development servers with `background=true` and `wait_process`, and
warns against shell-level `Start-Process`/`start` detachment. This follows a
TestProj run whose frontend build and real API probes succeeded but whose
foreground hidden-server command timed out, leaving child effects uncertain.
The focused provider-facing process contract verifies the guidance reaches
the model (Release build plus CTest **1/1**). This addresses tool guidance
only; the timed-out run remains failed, and pytest/xlang3 compatibility is
separately pending.

## Native CLI connection catalog management — 2026-10-12

The native CLI now lists connection profiles, saves and removes Local/Nexus
catalog metadata, and selects profiles through the authenticated local backend.
It validates the backend catalog before printing it, uses the catalog revision
for every mutation, and sends no provider or server credentials. An
unauthenticated Nexus selection fails closed and leaves Local selected. Release
`xmind_cli` and `xmind_server` build; the CLI, general HTTP CLI and connection
profile HTTP contracts pass **3/3**. This does not add browser or VS Code
selection, Nexus authentication, or remote command/event routing. See the
[connection profile contract](connection-profiles.md) and [exact test output](evidence/native-connection-profiles-cli-local-20261012.log).

## Authenticated Local/Nexus profile catalog API — 2026-10-12

The native local HTTP backend now exposes `GET /v1/connections`,
`POST /v1/connections` and `POST /v1/connections/select` over the existing
owner-authenticated transport. Local and same-machine origin-bound view
credentials can read and update the public catalog. Nexus endpoints are
reported as `not_connected`; unknown credential fields are rejected, and
selecting an unauthenticated Nexus entry returns a conflict without changing
the active Local profile. The primary Release `xmind.exe` target builds and
the focused profile, HTTP and owner/view-session contracts pass **3/3**.
Browser/CLI profile pickers, encrypted Nexus authentication, and remote
command/event routing remain unimplemented. See the [connection profile
contract](connection-profiles.md) and [exact test output](evidence/native-connection-profiles-http-local-20261012.log).

## Claude adaptive-turn handling and browser coding verification — 2026-10-12

Anthropic adaptive-thinking model families now receive a bounded 16,384-token
output budget and medium effort by default; legacy Claude families retain the
4,096-token default. Truncated agent turns report a specific output-limit
diagnostic, and incomplete Claude stream blocks map to safe, stable diagnostics.
The focused native request, stream, model-stream and provider-profile contracts
pass **4/4**. Commit `ac0ad69` is pushed to `main`.

In the external Edge xMind page, a Claude coding run targeted the current
`D:\CantorAI2026\TestProj` workspace and applied a model-reference copy control
to `model-zoo/frontend/src/views/ModelDetailView.vue` through the edit review.
The Vue production build passes and the live FastAPI API returns the actual GPT-2
detail record. The agent run itself ended with an Anthropic HTTP 400 after the
write, so full run completion and rendered clipboard interaction are still
unverified. Backend pytest did not run: `xlang3 -m pip install pytest` failed in
pip dependency resolution with `memoryview() requires a bytes-like object`;
tests were not run under CPython.

Follow-up inspection of the preserved local run records confirms that every
Claude `tool_use` ID has one matching `tool_result` in order for the failed
continuation. The profile database also has a trusted PowerShell process
profile (`auto_approve=true`, 600,000 ms maximum); this confirms that local
process execution is configured, but does not establish that the failed agent
run invoked it. The TestProj frontend production build passed again, and the
live FastAPI health and GPT-2 detail endpoints returned real records. The
Anthropic response body is deliberately not persisted because it may echo
prompt or credential data, so the exact provider-side reason for HTTP 400 is
still unknown. A fresh external Edge interaction could not be performed in
this Codex host because its external-browser surface is unavailable.

## xlang3 main integration and native Release gate — 2026-10-12

xMind now pins the pushed xlang3 `main` revision
`f0c425cff5b6bf47f18144b31e4dc1ac16c9a9f7`, which includes native SQLite
transaction and AES-GCM compatibility. A Release build with the embedded
xlang3 runtime passed all **138/138** native contracts in **173.58 seconds**.
The schema-17 migration preserves budget rows, dependent triggers and foreign
keys while admitting ordinary direct-agent budgets with zero child capacity.
Direct coding runs now recheck trusted process executable identities before
each model turn, so a changed executable ends the run before another provider
request. The long local-profile, local-view and provider-configuration
contracts passed. The xlang3-hosted pure-Python `cryptography` install remains
blocked by the separate missing `mmap` API; no native xlang3 change was made.
This is a source Release gate, not a claim of complete installed IDE or live
provider acceptance. See the [full test output](evidence/native-release-ctest-schema17-20261012.log)
and [current validation](VALIDATION_STATUS.md#xlang3-main-integration-and-native-release-gate--2026-10-12).

## xlang3 pytest and mmap compatibility candidate — 2026-10-13

The pushed xlang3 branch `fix/pytest-pathaware-hook-proxy` at `1df2603a` is
based on xlang3 `main` `f0c425cf`. It adds lazy pytest hook lookup, native
zero-copy mmap buffer exports with close/lifetime checks, and the Pydantic Core
2.50.1 pure-Python facade plus iterative schema-gather bridge. The full xlang3
Release suite passes **56/56 CTest contracts**, and TestProj's Model Zoo
backend passes **9/9** pytest API tests with Pydantic 2.14.1 on xlang3. An
isolated xMind Release rebuild against the candidate passes **137/137 native
CTest contracts** ([full log](evidence/native-xmind-xlang3-1df2603a-ctest.log)).
The 11-case Release performance gate passed on the prior branch checkpoint
before the facade update. xMind still pins xlang3 `main` `f0c425cf`; the
candidate has not been merged or deployed into the active xMind runtime. See the [detailed
validation and report](VALIDATION_STATUS.md#xlang3-pytest-and-mmap-compatibility-branch--2026-10-13).

## VS Code coding-flow verification against TestProj — 2026-10-12

The xMind right-sidebar extension targeted the opened `D:\CantorAI2026\TestProj`
workspace and reused the saved Claude profile. It applied scoped styling edits
to `model-zoo/frontend/src/style.css`; the run then stopped because xMind could
not validate the model response, before editing `HomeView.vue` or running its
requested build. Independent verification passed: the frontend production
build, xlang3-hosted FastAPI health/catalog/stats requests, and external Edge
checks for search, task filtering and model-detail navigation. This proves the
workspace binding, real file writes, API-backed page and these browser actions;
it does not pass the full coding run. A narrower Claude retry failed with the
same validation message after reading the Vue file, before making changes or
running a build. The run panel did not expose a safe protocol diagnostic code.
The backend pytest entry point remains incompatible with xlang3's current
pytest hook proxy. See [exact scope and outcomes](evidence/testproj-vscode-coding-flow-20261012.json).

## MCP catalogue invalidation and same-run refresh — 2026-10-12

Run-owned MCP list-change notifications and unexpected listener loss now
invalidate the catalogue shared by the notification observer and native registry. The registry checks before
proposal and after human approval, so stale bindings never reach a remote
effect. AgentRunner reports the blocked call to the model, rediscovers tools,
and continues with current definitions. Direct graph MCP nodes use pinned
aliases, so they fail closed and require a graph revision when the peer
catalogue changes. HTTP integration contracts cover AgentRunner refresh and
graph fail-closed behavior; the native effect contract exercises a catalogue
change during approval and confirms durable operation retirement without
dispatch. Crash recovery/reconnect and live external-peer acceptance remain
open. Validation is recorded in
[current validation](VALIDATION_STATUS.md#mcp-catalogue-invalidation-during-a-run--2026-10-12).

## MCP run-owned subscriptions for agents and graphs — 2026-10-12

`McpToolClient` exposes `listen_subscriptions` for modern HTTP and stdio peers,
including an immediate callback for the validated acknowledgement. AgentRunner
owns a separate listener per peer advertising list-change notifications,
journals sanitized acknowledgement/notification/closure events, caps each
listener at 64 notification records and joins listeners before a run becomes
terminal. Held-open stdio and Streamable HTTP AgentRunner contracts verify
70-event bursts, payload/credential exclusion, overflow reporting, stream
cancellation and closure ordering. Direct graph MCP children now use the same
observer and child journal; the graph fixture verifies closure after approved,
denied and cancelled tool operations; a second graph fixture verifies held-open
Streamable HTTP listener shutdown before child completion. The HTTP AgentRunner
also proves its MCP tool waits for controller approval and its listener closes
before `run.completed`.
Graph, AgentRunner, HTTP/stdio and official modern/legacy SDK contracts pass
**10/10**. Crash recovery/reconnect and live external-peer acceptance remain
open. [Validation and limits](VALIDATION_STATUS.md#mcp-2026-07-28-subscriptionslisten-for-agents-and-graphs--2026-10-12)
and [exact test output](evidence/native-mcp-subscription-lifecycle-local-20261012.log).

## MCP modern HTTP subscriptions/listen — 2026-10-12

The native Streamable HTTP client now supports MCP's `2026-07-28`
`subscriptions/listen` request-scoped SSE stream. It validates the required
first acknowledgement, accepted filters, subscription IDs, notification
types/URIs and graceful completion; caller cancellation is wired to close the
HTTP response stream. A real loopback peer verifies resource and tools-list
updates plus fail-closed behavior for an unrequested prompt-list update. The
Release client target builds; the focused contract passes **1/1** and the six
dependent wire/client/graph/AgentRunner/unified HTTP contracts pass **6/6**.
HTTP AgentRunner lifecycle is subsequently covered by the expanded held-open
integration in the preceding milestone; reconnect and broader live SDK
acceptance remain open. See [validation scope](VALIDATION_STATUS.md#mcp-2026-07-28-http-subscriptionslisten--2026-10-12)
and [exact test output](evidence/native-mcp-subscriptions-http-local-20261012.log).

## Live coding acceptance through the xMind browser — 2026-10-12

The actual local xMind page ran a scoped DeepSeek coding task against
`D:\CantorAI2026\TestProj`. It fixed stale asynchronous catalog responses in
`HomeView.vue`; the post-edit Vite production build passed. The rendered Model
Zoo app then passed live search, task filtering, no-result/reset, pagination,
sorting and detail navigation against its already-running local API. Commands
used the trusted local PowerShell profile without command-by-command approval;
the one file edit was separately approved. This is browser coding-flow
acceptance for TestProj, not VS Code, CLI, or starting the project's services.
See [exact scope and limits](evidence/testproj-model-zoo-live-browser-20261012.json).

## Recover A2A tasks observed before a stream failure — 2026-10-11

When a remote A2A stream delivered a task ID and a later event failed to persist,
the send was correctly marked uncertain but its known task/context IDs were
discarded. The native effect journal now retains those observed IDs. Its
owner-authenticated inspect route can fetch current state from the same pinned
peer after the xlang3/SQLite store is reopened. Inspection is read-only: the
initial send remains uncertain, is never retried, and does not gain cancel or
follow-up actions. An uncertain send with no task ID remains quarantined without
peer contact. Release compilation passed; three native A2A contracts, **314**
VS Code tests and **96** browser tests pass. The callback failure and peer are
synthetic; live external A2A/installed-client interoperability remains open.
See [exact scope and evidence](evidence/native-a2a-observed-task-recovery-local-20261011.md).

## Release validation checkpoint — 2026-10-11

The complete native Release build and all **136/136** CTest contracts pass.
Rust-analyzer semantic navigation first returned `null` within the test's
15-second retry window. The native contract now allows 60 seconds for semantic
readiness; focused and full reruns pass, though the evidence does not establish
whether cold indexing or system load caused the first miss. Long local-profile,
local-view and provider-configuration contracts also pass. This validates
source-level Release behavior, not installed IDE acceptance. See the
[current validation record](VALIDATION_STATUS.md),
[full CTest output](evidence/native-current-release-ctest-retry-20261011.log),
and [focused rust-analyzer output](evidence/native-rust-analyzer-focused-20261011.log).

## Single-agent file tools use admitted workspace membership — 2026-10-11

The managed backend receives the protected local xlang3/SQLite workspace
registry path. Each single-agent root run transactionally captures its current
workspace revision and opens/identity-checks every member. Native read, list,
search and glob tools accept an explicit registered `folder_id`; omission keeps
the primary-folder default. Results include workspace, folder and revision
provenance. The durable root-run snapshot is reused by its admitted agent
children, so attaching another folder mid-run cannot redirect that run; a later
root run observes the newer registry revision. Five focused native contracts
pass, including an actual AgentRunner/provider/tool loop reading a same-named
file from the attached root, plus local-profile launch/handoff, registry and
local-view coverage. Graph-node tool routing, edit/approval/process/Git/LSP
folder routing, and CLI/VS Code membership controls remain open. See the
[multi-folder workspace contract](native-multi-folder-workspaces.md).

## Native browser previews across attached workspace folders — 2026-10-11

The local browser now renders one file tree per registered workspace folder.
Folder reads carry the member folder ID; the native local-view gateway resolves
that ID only inside the active workspace, reopens the folder and compares its
native identity before reading. The local-view contract checks two roots with
the same relative filename, distinct content and rejection of an unregistered
folder. Agent file tools/edits, processes, Git and language servers still use
the primary folder; this checkpoint enables read-only browser previews across
members. See [native multi-folder workspace contract](native-multi-folder-workspaces.md).

## Native workspace-group registry foundation — 2026-10-11

The profile-aware local view now records a workspace group in a protected
local SQLite database accessed only through embedded xlang3. A workspace is
anchored to its primary directory identity; folder attachments retain verified
native directory identities and ordinals, and increment a durable membership
revision. Registry checks reject duplicate, nested/overlapping and
profile-storage roots. The local workspace catalogue returns the persisted
revision, primary folder and folder list. The local browser can create a
workspace from one folder, then attach another folder to the same workspace
without switching its active session. The focused registry contract verifies
reopen plus concurrent attachments, and the native local-view contract checks
actual create, attach, duplicate rejection and unchanged workspace identity.
Agent tools, approvals, processes, Git, language servers and VS Code/CLI folder
controls still use only the primary root.
See [native multi-folder workspace contract](native-multi-folder-workspaces.md).

## Latest native LSP and profile validation — 2026-10-11

The native language-server client now retries transient request-cancelled,
content-modified and server-cancelled replies with a bounded delay; the focused
Rust Analyzer contract passes three times consecutively. The local-profile
upgrade contract also passes after raising its subprocess ceiling from 90 to
180 seconds (**1/1 in 161.79 seconds**). A current full Release gate passed
dependency verification, CMake configuration and `ALL_BUILD`; newly active
xlang3 benchmarks caused its contract guard to defer before CTest. The full
suite remains unverified after the timeout adjustment.
[Focused evidence and limitations](evidence/native-lsp-retry-focused-20261011.md).

The preceding complete Release gate passed **134/134** contracts in one run
after the final delegation-migration and CLI-exit fixes. It covers native
AgentFlow, MCP/A2A, persistence, profiles, process tools and local view
integration; it does not prove packaged or rendered IDE acceptance.
[Exact earlier CTest output](evidence/native-full-release-ctest-20261011.log).

## Current browser and VS Code source gates — 2026-10-11

At the current checkout, the VS Code extension suite passes **310/310** and the
browser client suite passes **95/95**. Exact output is preserved in
[VS Code evidence](evidence/vscode-current-source-20261011.log) and
[browser evidence](evidence/browser-current-source-20261011.log). The installed
VS Code extension is `agentflow-local.agentflow@0.1.10`; these contract suites
do not establish that the sidebar rendered or connected in the active editor.
Windows desktop inspection was attempted but denied access, so rendered VS Code
acceptance remains open. The connected browser separately displays the four
saved provider profiles and their available model selection; secret key values
remain backend-owned.

## Saved-key native CLI process run — 2026-10-11

The current Release `xmind_server` and `xmind_cli` ran a real DeepSeek
`deepseek-flash` turn using the encrypted key imported from `.config/providers.yaml`.
The isolated profile began key-only; the backend discovered models through the
saved key and activated the selected model before CLI admission. The agent
proposed one PowerShell command, the CLI displayed the pending operation, and
approval was granted only after checking the recorded executable, fixed
arguments and workspace. The process printed the expected marker, the backend
persisted its successful result, the run completed, and provider usage metrics
were retained in conversation history. No process profile was added to the
repository configuration. This verifies the live native CLI process path for
one provider and one harmless command; rendered browser/VS Code process
acceptance and broader shell parity remain open. [Sanitized evidence](evidence/native-live-cli-process-acceptance-20261011.json).

## Live browser coding write uses saved provider and approval — 2026-10-11

The connected xMind browser completed a real DeepSeek coding run that requested
one `create_file` operation, displayed its exact path/content and awaited the
native approval. After approval, the operation and run completed, and the
sidebar displayed provider-supplied input/output metrics. The one-line smoke
file was written under the ignored `build/` directory, verified byte-for-byte,
then removed; the repository working tree remained clean. The running native
revision was `3681c2e`, an ancestor of current source but not byte-identical to
the current Release binary. This verifies one browser create-file workflow;
live VS Code coding, shell/process execution and full OpenCode parity remain
open. [Sanitized evidence](evidence/live-deepseek-browser-create-file-20261011.json).

## Interactive console can cancel or detach from a live run — 2026-10-11

While the native CLI watches a streamed run, `/cancel` sends cancellation to
the shared backend and continues observing until its terminal event. `/exit`
detaches the console and leaves the server-owned run running. Ordinary input
typed during a run waits until the current turn ends. The Release build and
seven focused native/CLI CTest contracts pass locally, covering single-agent
cancellation and detach, graph observation and checkpoints, approvals,
context, skills, HTTP CLI and provider-profile commands. Full CTest, hosted
package acceptance and full OpenCode interactive-CLI parity remain open. See
the [interactive CLI contract](native-interactive-cli.md).

## Saved provider keys are clearly identified in client Settings — 2026-10-11

The shared browser and VS Code renderer now says when the selected profile has
a saved key available on the local xMind backend, and that users do not need to
enter it again. The password field remains empty; the key itself and its
reference are never sent to the renderer. The actual browser Settings panel
showed the indicator for the active DeepSeek profile. Package `0.1.9` was
verified and installed. VS Code tests pass **298/298** and browser tests
**85/85**. A new rendered VS Code sidebar has not been verified, and native
inference or full parity is unchanged. [Evidence](evidence/provider-key-status-live-browser-20261011.json).

## Explicit trusted runtime refresh in VS Code — 2026-10-11

VS Code now registers **xMind: Apply Trusted Runtime Configuration**. After
the user confirms, the host reconnects with Native's protected
`--upgrade-backend` path, allowing changed trusted process-profile contents to
be imported without losing sessions or history. Host tests verify cancellation
does not request the upgrade, confirmation requests it once, and a successful
launch consumes that request. The full extension suite passes **298/298**.
VSIX `0.1.8` packages the verified native runtime (1,899 runtime files), was
installed locally, and appears as `agentflow-local.agentflow@0.1.8`. Real
backend confirmation/handoff and an approved process command in the rendered
sidebar remain unverified.

## Managed views load trusted process profiles — 2026-10-11

Commit `b0dd491` passes trusted process-profile configuration through the
managed browser and VS Code launchers into the native `xmind view` process.
Native code imports the file into the xlang3-backed registry. A changed file
requires a protected backend-owner handoff; existing sessions, history and
graph results survive the restart. Removing the file after import leaves the
persisted registry available. The native local-view contract also reads the
sanitized process-profile catalogue through view authentication. VS Code now
has an explicit confirmation-gated command to apply trusted runtime/process
configuration using the protected owner handoff. The focused native
profile/view contracts pass, VS Code passes **298/298**, and browser
tests pass **85/85**. The full native gate and rendered managed-client process
run remain unverified, and no shell profile is seeded. See [validation
status](VALIDATION_STATUS.md) and [process tooling](native-process-tools.md).

## Saved provider key discovery recovers from stale profile state (2026-10-11)

The local browser initially received a provider-profile revision conflict while
discovering models for the saved DeepSeek profile. Refreshing the same page
restored its latest profile metadata and the native backend fetched two models
using the encrypted saved key; the browser received only model IDs and a status
message. The Settings password field stays blank by design when reusing a
saved key.

Commit `5a434a0` makes this recovery automatic in the shared browser/VS Code
profile controller: after a revision conflict it refreshes once and retries
saved-key discovery only if the same profile, route and active selection still
match. It never retries an unsaved key or a changed selection. The complete
extension suite passes **295/295**; native local-view, local-profile and shared
provider-config contracts pass **3/3**. Installed VS Code packaging and full
OpenCode parity remain separate work.

## Local xMind 0.1.7 VS Code package preview (2026-10-11)

The browser assets now rebuild from the same saved-key recovery controller as
the VS Code client. After rebuilding the live browser assets and reconnecting,
the real page fetched its saved provider's model catalogue without showing or
requesting the secret again. The UI confirmed that the key stayed in local
xMind. The Settings key field is intentionally blank when reusing a saved
credential.

The local 0.1.7 VSIX uses the current xMind Release binary and the configured
xlang3 Release runtime/modules, with source hashes verified in its native
runtime manifest. The complete native suite passed **132/132** and the VS Code
suite passed **295/295**. The VSIX contains 1,899 runtime files and passed the
20-asset/private-state package check. It installed and activated as
`agentflow-local.agentflow@0.1.7` in the isolated package-test profile.
[Package evidence](evidence/vscode-0.1.7-package-preview.json). The xlang3
checkout at `8151a334` was dirty when its already-built binaries were packaged;
this is a local preview, not a clean reproducible release artifact. No xlang3
files were changed by packaging. The broader OpenCode feature-parity audit and
rendered VS Code sidebar acceptance remain open.

## Native MCP resources and prompts (2026-10-11)

The C++ MCP clients now list resources/prompts and read resources/get prompts
over stdio and HTTP. The native agent and graph registries expose them as
approval-gated model tools, bind exact peer metadata, schemas and resource URIs
into durable operations, journal returned data, and reject peer responses that
reflect configured credentials. Existing tool approval records retain their
stored shape for dynamic-plan compatibility.

The Release build passes ten focused native CTest contracts covering real
stdio peers, modern and legacy protocol behavior, approval/denial, dynamic-plan
compatibility, graph MCP, existing HTTP MCP flows, and a local Streamable HTTP
peer for resource pagination/read and prompt list/get. The pinned official MCP
server SDK 2.3.1 now also runs resource/prompt discovery and approved or denied
retrieval through the configured native HTTP client, registry and durable
xlang3-backed journal; allowed and denied outcomes survive reopen. The combined
UI renderer and browser-controller tests pass **117/117**. Subscriptions,
additional HTTP/SSE behaviors and broader live MCP coding remain incomplete.
The implementation and contracts are checked in on
`checkpoint/native-persistence-m1`.

## Saved provider profiles work in a fresh local browser view (2026-10-10)

A fresh same-machine page connected to its local xMind process without a server
token or provider-key prompt, then displayed the four saved providers and the
active OpenAI model catalogue. The key remains backend-owned; the Settings key
field is blank when reusing it. Both the in-app browser and external Microsoft
Edge completed real OpenAI `gpt-5.6-sol` requests with the saved profile; each
page showed provider-reported tokens and backend-measured timings. The older
`60415` tab was unlinked and is not the working view. [Exact rendered acceptance
and limits](evidence/browser-live-saved-profile-ui-20261010.json).

## Full local native and client gate restored (2026-10-10)

The Release `ALL_BUILD` and complete native CTest gate passed **122/122** in
**547.58 seconds** on source `10d2a9a`. This includes current passing reruns for
the managed-profile and local-view Unicode-path startup, xlang3 repository
open, and backend handoff tests that were listed as failures in the earlier
`60e454c` checkpoint. The matching source passes **272/272 VS Code extension**
tests and **75/75 browser** tests. A fresh native/browser integration run also
passed real graph pause/input/resume and workspace read, browser/session and
backend restart, saved-history retention, and no execution replay. Only the
disposable backend was restarted; no model or provider was simulated in that
integration. Raw CTest, client logs, artifact hashes and exact limits are in
[local gate evidence](evidence/native-122-and-views-local-20261010.json).
These local gates do not establish hosted release packaging or current
rendered/installed VS Code interaction; full OpenCode parity remains open.

## Saved provider keys on the rebuilt local browser (2026-10-10)

Using the rebuilt native runtime and the repository's single
`.config/providers.yaml`, the same-machine browser page discovered live model
catalogues for Claude, Gemini, DeepSeek and OpenAI (13, 11, 2 and 20 models).
Each profile then completed a real short text request with its saved backend
credential: Claude Haiku 4.5, Gemini 3.8 Flash, DeepSeek Flash and OpenAI
gpt-5.6-sol all returned `OK`; the UI displayed provider-supplied token usage
and measured timings. No provider key value or server token was requested in
the connected page. The exact native executable, xlang3 runtime, local runtime
manifest, run IDs and metrics are recorded in
[live provider evidence](evidence/live-provider-profiles-current-native-browser-20261010.json).
This is live text-only acceptance on an unsigned local development runtime;
it does not prove streaming, tool use, coding edits, installed VS Code
acceptance or complete provider compatibility.

## Live saved-provider catalogues in the local browser (2026-10-10)

From the connected xMind browser, selecting each saved Claude, Gemini,
DeepSeek and OpenAI profile made the UI fetch and display its live model
catalogue using the backend-held credential. The model selectors populated with
13, 11, 2 and 20 models respectively. No key value was returned to the page;
the previous DeepSeek / `deepseek-flash` selection was restored afterward.
This verifies provider catalogue authentication and rendered population, not
inference or streaming. [Exact evidence](evidence/provider-catalogues-live-browser-20261010.json).

## Live Gemini inference and native file-tool call (2026-10-10)

The saved Gemini credential completed a real UI inference on the discovered
`models/gemini-3.8-flash` model, then used the native `read_file` tool to read
the repository README and return its first heading (`xMind`). No files were
changed. The earlier `models/gemini-2.5-flash` attempt returned HTTP 404, so
catalogue discovery alone does not establish that every discovered model is
usable by an account. The UI kept the credential backend-side and was restored
to DeepSeek / `deepseek-flash` afterward. This verifies one live Gemini model
and one read-only tool call; it does not establish full Gemini capability or
coding-workflow parity. [Exact evidence](evidence/live-gemini-inference-20261010.json).

## Native outbound A2A SDK and CLI contract repair (2026-10-10)

The native outbound client now passes the pinned official A2A JavaScript SDK
1.3.0 peer for authenticated AgentCard discovery, `SendMessage`, `GetTask`,
wrapped `SendStreamingMessage` events and `CancelTask`. The contract accepts
the SDK's legal `Task` result wrapper. Windows response-header enumeration now
preserves all `WWW-Authenticate` challenges, accepts repeated identical
`A2A-Version` values while rejecting conflicting values, and is checked by the
native HTTP transport contract. CLI A2A inspect/cancel send the required empty
JSON object and return the server's real 404 for unknown operations.

The Release build passed and all **7 A2A CTest contracts** passed; the focused
Windows transport and CLI HTTP contracts also passed. The full **122-test
native gate passed 118/122**. Four unrelated managed-profile/repository/handoff
contracts remain open: local profile and view startup fail converting a Unicode
workspace path in the current Windows locale, `xlang_repository_contract`
reports that it cannot open its database, and the owner-handoff fixture reports
`No module named '_sqlite3'`. The latter two require xlang3/runtime diagnosis;
they are not counted as passing. Provider replies in these contracts are
synthetic; this historical `60e454c` checkpoint does not claim full A2A or
OpenCode parity. Its four listed native failures were subsequently rerun and all
passed at the later **122/122** checkpoint above.

## Same-machine browser reconnects to the active xMind process (2026-10-11)

The browser adapter now resolves its matching local launcher credential when a
page connects, so a view started before xMind publishes its active profile can
attach without asking for a server token or requiring the user to find a new
URL. The credential remains in the adapter process and the browser receives
only its scoped HttpOnly view session. The focused browser connection and
adapter suites pass **26/26**; the complete browser suite and hosted isolated
gate are pending. This does not remove native owner authentication.

## Saved-provider model picker guidance (2026-10-11)

When saved provider profiles exist but none is active, the footer model picker
now directs the user to choose a saved provider below. If the active profile
has no discovered model list, it explains that discovery can be retried using
the saved profile in Settings; it no longer asks the user to re-enter a key.
With no saved profiles, it directs the user to add one in Settings. This copy
change does not activate a provider or alter model-selection behavior. The
focused renderer suite passes **74/74**, the complete VS Code suite **269/269**,
and the browser suite **73/73**. These tests use synthetic provider metadata;
they do not establish live provider discovery or installed-client acceptance.

## Outbound A2A task cancellation (2026-10-10)

The native server now has an explicit, one-shot `CancelTask` route for a saved,
acknowledged outbound task. The intent is durably recorded before dispatch and
is bound to the admitted peer revision and task ID. A second request is rejected;
timeouts and uncertain responses require task inspection. CLI, browser and VS
Code expose explicit cancellation, with a browser or editor confirmation, and
inspection reports the durable cancellation outcome after reload. The browser
and extension contracts pass locally (**71/71** and **267/267**). The native
Windows contract has not yet been built or run; CMake/MSBuild are unavailable in
this development shell, so the pushed Windows workflow must verify it. A2A SDK
interoperability, durable stream resumption, input-required follow-up, artifacts
and full task recovery remain open.

The outbound A2A client provides capability-gated SSE `SendStreamingMessage`
and `SubscribeToTask` methods for protocol 1.0 and 0.3. AgentRunner now routes
advertised streaming sends through that client and persists each returned task
event in the run journal; non-streaming peers retain blocking `SendMessage`.
The registry contract covers two live events in the A2A 1.0 `StreamResponse`
wrappers (`task` and `statusUpdate`) and durable event counts. The pinned
official JavaScript SDK peer advertises and serves streaming; its own
`sendMessageStream` self-check passes, and the native CTest contract now
consumes its wrapped events. The native executable contract is awaiting the
hosted Windows gate. The shared activity panel labels A2A events with peer,
status, task ID and sequence. Inbound resubscription/recovery, rendered live
browser streaming and installed VS Code acceptance remain open. VS Code's
renderer contract passes **73/73**, the full extension suite **268/268**, and
the browser suite **72/72**.

Managed profile runtime upgrade is now wired into the native profile launcher.
The console supports `--upgrade-backend`; the native browser/VS Code adapter
requests it automatically when its verified package differs from the saved
runtime. The flow stages and verifies the replacement, uses the authenticated
workspace authority and owner receipt to retire the old process, then starts
the replacement on the same port and SQLite profile before activation. A new
synthetic older-generation contract checks session/history/run continuity and
port stability. A clean native Release build against the current xlang3 runtime
passed before the active-work retry adjustment in
`build/native-milestone-clean`; the current source needs a rebuild. CTest is
deferred while the live xlang3 benchmark runs. The profile-upgrade native
contract and installed continuity acceptance remain pending. Separate live
provider and webpage checks on the older managed preview are recorded below;
they do not verify these source changes. The isolated view workflow passed
**264 VS Code and 63 browser contracts**, with all tracked sources, vendored
files and browser assets unchanged. See the exact
[run and artifact evidence](evidence/view-contracts-c7fbc194.json).

Current desktop inspection confirms the live VS Code TestProj pane is still
waiting for workspace connection and displays `Authentication required`. The
separate browser tab on port 60405 still waits for a native-server token and
its workspace catalogue returns HTTP 401. The profile-aware browser on port
60415 did connect to the managed local profile without entering a server
token; its live provider and webpage results are recorded below. Same-machine
xMind view and VS Code launches enroll through their local owner and keep that
credential inside the adapter. A token prompt belongs only to a separately
attached remote server client.

The shared provider YAML is now verified against the current native runtime
and real embedded-xlang3 SQLite: a disposable profile imported Claude,
DeepSeek, Gemini and OpenAI at registry revision 1, with no provider or model
network requests. The existing VS Code `TestProj` profile remains on runtime
`57da085`; its live catalogue is empty. That owner supports authenticated
quiesce/retire/activate, but its runtime has no YAML-import endpoint. The
current native view adapter removed the earlier JavaScript owner-upgrade
client, and no replacement updates the encrypted local `profile.state` yet.
Profile-preserving owner handoff from the current native view remains open;
the precise live and isolated evidence is in
[provider-import-live-local-20261010.json](evidence/provider-import-live-local-20261010.json).

Live webpage provider checks used the actual local managed profile: Claude
returned a real `OK` response with recorded usage and timings, Gemini model
discovery succeeded but inference failed with HTTP 404, DeepSeek left an
orphaned `running` record whose Stop action reported no executor owner, and
OpenAI model discovery returned 20 models; a fresh OpenAI `gpt-4.1-mini` run was
admitted but stayed `running` at `budget.model_call.started` with `Illegal
invocation` displayed in the page, and Stop reported that the executor no longer
owned it. Workspace switching was mixed: one connected
tab successfully switched xMind → TestProj → xMind, while another failed with
`Illegal invocation`. These results and run identities are recorded in
[live provider acceptance](evidence/live-provider-browser-acceptance-20261010.md);
the pending profile-upgrade source has not been built or installed in this older preview.

Schema-v14 checkpoint `15c87c0` passes the complete local Release native gate:
**118/118 contracts** in **366.56 seconds**. It updates disposable historical
database fixtures to remove only empty agent selections, refreshes current-schema
assertions and lets the legacy owner handoff safely snapshot current schema-v14
repositories. The [complete CTest log and provenance](evidence/native-schema14-local-20261010.json)
are recorded. Client sources did not change; the existing rendered webpage
workspace acceptance remains **61/61**. OpenCode parity gaps and live model-backed
coding acceptance remain open.

The current Windows native build passes rendered standalone-webpage workspace
acceptance at source `ed5b84d`: the 648-pixel page registered a second isolated
workspace, switched between both folders, and restored the selected workspace
after refresh without re-entering the server token. No provider request or
workspace-file change occurred. The browser suite passes **61/61**. Earlier
current-browser evidence also verified CLI reattachment to an existing profile
owner; same-session/event inspection across clients remains a separate gate.
[Exact current rendered evidence](evidence/browser-workspace-live-ed5b84d.json).

The installed VS Code `0.1.5` extension also opened the actual `TestProj`
folder in a new VS Code window and launched its managed native view for that
same root. Its ready record confirms reattachment to the existing TestProj
profile owner; **263/263** extension tests pass. The native view process and
workspace binding are verified, while rendered sidebar inspection remains
unverified because this session exposes browser automation but not a native
VS Code window. [Live workspace evidence](evidence/vscode-open-folder-live-20261010.json).

At source `59f46791c02cb18cd4aa4c66b7d01b22f06cbcb0`, managed local-profile
startup verifies the complete source runtime once, then authenticates protected
profile state and uses the verified managed-copy path for server startup and
workspace reattachment. This removes redundant full-package scans while keeping
complete explicit revalidation available; a changed managed runtime is rejected
by the verifier contract. The rebuilt Windows binary passed the local-profile,
runtime-generation and native/browser workspace contracts. The standalone
webpage suite passed **60/60**. The currently open webpage still uses an older
backend and keeps Add workspace disabled; fresh rendered acceptance against the
rebuilt backend remains open. [Exact scope](evidence/native-profile-workspace-59f4679.json).

Native authoring now accepts bounded YAML graph catalogs plus JSON/YAML/Markdown
agent-instruction supplements. The new YAML converter rejects duplicate keys,
aliases, multiple documents and oversized values before existing native graph
validation; accepted records persist through embedded xlang3 SQLite. Graph and
instruction persistence tests passed in the complete **118/118 native CTest**
run. This is the authoring foundation only: named agents/tools, skills, `.py`
callables and the xlang3 programming API remain open. [Supported formats and
limits](authoring.md).

The current `native_local_view_contract` passed at source `4701255` in 188.95
seconds. The actual native backend, browser adapter and production browser
controller completed workspace registration, scoped incremental graph events,
cookie/cursor reconnect, detach/reattach, stream-capacity and view-owner
revocation checks. In the same run, production VS Code controller/backend code
completed a native pause/input/read/approved-create path and matched the actual
disk bytes to the native receipt. No provider calls were made. VS Code APIs are
fixtures; a rendered browser/IDE and installed-client acceptance remain open.
[CTest output and provenance](evidence/native-local-view-browser-sse-local.json).

The local managed-profile startup repair now passes the native profile contract
**1/1** in **76.79 seconds** and runtime-generation contract **1/1** in **6.07
seconds**. The copier now closes its native manifest writer before the read-only
inventory verifier reopens that file; transient Windows sharing violations get
a bounded retry. A live CLI `profile-info` also reattached to the same backend
PID and `D:\CantorAI2026\TestProj` workspace as the VS Code launch (`started`
false). This is local source/profile evidence, not a rebuilt release package or
full rendered VS Code acceptance.

The storage source `57da085` passes the complete hosted **118 native** gate in
**360.18 seconds**, **257 extension / 55 browser** checks and packaging. Independent
downloads verify exact inventory, both archive digests, all runtime/source-bundle
bytes, sixteen packaged-source comparisons and twelve accepted browser assets.
Actual durable xlang3 SQLite storage, trusted synthetic login/refresh primitives
and owned machine-root cleanup are verified. The subsequent manual renewal
coordinator and client controls require separate acceptance; this older source
does not establish their integrated exchange.
[Exact hosted storage evidence](evidence/native-oauth-refresh-storage-hosted-57da085.json).

The repaired `0eee494` hosted source passes all **117 native** contracts in
**344.09 seconds**, **257 extension / 55 browser** checks and packaging. Original
output confirms actual trusted native OAuth login/registered callbacks, encrypted
grant reopen, seven separate single-use refresh protocol modes and owned machine
root cleanup. Both artifact digests, all runtime/source-bundle bytes and packaged
views verify independently. This precedes the newer durable storage candidate and
does not prove automatic renewal, real authority or installed/rendered acceptance.
[Exact hosted evidence](evidence/native-oauth-refresh-hosted-0eee494.json).

The durable refresh storage candidate passes the complete rebuilt **117/117**
local native gate in **389.30 seconds**, with all **2464** inputs unchanged and
three focused contracts passing. Actual xlang3 SQLite verifies exclusive claims,
credential/quiescence fences, atomic ciphertext/receipt rollback/publication and
generation-bound restart retirement. Client source is unchanged; client suites
were not rerun. The required hosted inventory is **118**. Automatic renewal,
network coordination and client recovery remain unfinished. Both original focused
failures are preserved. [Exact local evidence](evidence/native-oauth-refresh-storage-local.json).
[Candidate and acceptance requirements](native-mcp-refresh-storage.md).

The native checkpoint `6afaaf7` passes the complete **116/116**
local native gate in **382.06 seconds**, with all **2462** frozen inputs unchanged.
The unchanged original managed-profile contract passes in the full gate, and
the two focused refresh contracts pass. Client source is unchanged from the
prior complete **257 extension / 55 browser** result; those suites were not rerun.
This proves the exchange primitive and scoped startup behavior, not durable or
automatic refresh, trusted native login, installed clients or full MCP parity.
[Exact local evidence](evidence/native-oauth-refresh-local.json).

The registered-callback hosted gate at `6708269` passed **115 of 116** native
contracts in **407.85 seconds**. The trusted contract stopped before native login:
test-certificate import into `CurrentUser\Root` rejected noninteractive use.
The downloaded artifact digest, exact inventory and original logs are verified.
Trusted acceptance remains unproved. The subsequent test-only repair uses the
elevated isolated runner's LocalMachine Root store, preserving exact CA ownership
and verified removal. Actual hosted execution of that repair remains pending.
[Original hosted failure](evidence/native-oauth-registered-hosted-6708269.json).

The native single-use refresh exchange primitive compiles and passes two focused
contracts, including independently decoded forms and real untrusted HTTPS contact
with zero HTTP dispatch. Durable refresh ownership/publication, automatic refresh,
running-owner coordination and client refresh controls remain unimplemented. The
first complete rebuilt 116-contract local gate passed **115** and failed managed
profile startup at its unchanged 60-second fixture deadline, in **376.95 seconds**.
The original run and retained fixture are preserved; the original focused repeat
failed at the same deadline. A longer-watchdog diagnostic completed, with a
57,196 ms cold start; it is not acceptance. Directory preparation now prepares
and pins each destination once; the rebuilt original focused contract passes in
75.40 seconds with its unchanged per-command watchdog. The following complete
gate passes as recorded above. The mandatory hosted inventory now
requires 117 contracts. [Original failure](evidence/native-oauth-refresh-first-failed.json),
[primitive and required production integration](native-mcp-refresh.md).
[Original focused startup pass](evidence/native-oauth-refresh-startup-local.json).

The console source `73ccf90` passed its complete hosted **114 native** gate in
**417.25 seconds**, **257 extension / 55 browser** suites and packaging. Independent
downloads verify both artifact digests, exact native contract inventory, all
**1860** runtime and **29** source-bundle files, **16** packaged-source comparisons
and **12** independently accepted browser assets. This package precedes the
registered-callback and refresh changes. No current installation, rendered UI or
trusted OAuth login is implied.
[Hosted console package evidence](evidence/native-oauth-cli-hosted-73ccf90.json).

The registered-callback candidate adds backend-owned OAuth callback path/port
configuration, exact exclusive binding and credential/agent authority checks.
The complete rebuilt **115/115** native gate passes in **388.83 seconds**, with
**2460** unchanged mapped inputs. Focused checks also pass **four** selected
contracts, including **13** real-socket callback cases and actual xlang3 SQLite
configuration/grant reopen. The unchanged clients retain their prior complete
**257 extension / 55 browser** results; their suites were not rerun here. The
strengthened trusted-login fixture compiles; hosted positive native OAuth
acceptance remains pending.
[Implementation and verification boundary](native-mcp-registered-callback.md).
[Exact local evidence](evidence/native-oauth-registered-local.json).

The trusted OAuth candidate passed the complete **115/115** local native gate
in **386.74 seconds**, plus **257 extension / 55 browser** checks, with **2459**
unchanged mapped inputs. The new independent HTTPS authority contract passes;
the actual native trusted-login fixture is compiled but deliberately not run
on the development PC. This is not successful native OAuth acceptance.
[Exact local evidence](evidence/native-oauth-trusted-local.json).

Trusted HTTPS OAuth acceptance is prepared as a mandatory isolated Windows
contract. The independent synthetic authority verifies S256, one-use codes,
resource/client/redirect binding, callback faults and authenticated MCP replies.
The compiled C++ fixture must acquire a grant through the actual native service,
persist/reopen it through xlang3 SQLite and use it through the production MCP
factory. The complete hosted inventory now requires **116** contracts. The
desktop certificate helper and hosted-only CMake option both reject locally;
the desktop's CurrentUser Root thumbprints remain unchanged. Successful hosted
native login is still unverified. [Acceptance boundary](native-mcp-trusted-acceptance.md).

The corrected `73ccf90` hosted view job passed all **257 extension / 55 browser**
checks. Independently downloaded evidence verifies its artifact digest, all
**45 tracked / 47 including vendor inputs**, unchanged inputs/assets and twelve
regenerated assets against exact accepted source bytes. This is scoped client
acceptance, with no native login or installed/rendered claim.
[Hosted view evidence](evidence/native-views-hosted-73ccf90.json).

Native MCP console controls pass the complete **114/114** local native gate in
**388.18 seconds**, plus **257 extension / 55 browser** checks, with **2455**
unchanged mapped inputs. The actual `xmind` console starts/observes/watches/cancels
the production C++ service with embedded-xlang3 encrypted grant metadata and an
independent untrusted TLS peer. Synthetic HTTP fixtures verify automatic native
revision selection, generated/published request identity, exact CAS admission,
terminal observation without cancellation dispatch, interactive detach, owned
NDJSON observation, strict metadata/HTTPS link validation and non-reflecting
errors. The original first compilation failure is retained. Trusted HTTPS
positive OAuth login, actual browser launch, registration, refresh/revocation,
running-owner coordination and installed/rendered acceptance remain unfinished.
[Exact evidence](evidence/native-oauth-cli-local.json),
[console contract](native-mcp-console.md).

The prior `f595205` hosted view job failed **before executing its Node suites**:
the three added test files and updated test totals were missing from the CI
inventory. The reviewed guard now requires all **45 tracked / 47 including vendor
inputs** and **257 extension / 55 browser** tests, without removing/skipping any
tests. Local full suites pass; the corrected hosted acceptance is recorded above.
[Original job log and exact failure](evidence/native-mcp-login-view-ci-f595205.json).

Native MCP login service and browser/VS Code Settings controls pass the complete
**113/113** local native gate in **378.02 seconds**, plus **257 extension / 55
browser** checks, with **2454** unchanged mapped inputs. The actual C++ async
service uses embedded-xlang3 encrypted grants and authenticated HTTP/View routes.
An independent self-signed HTTPS peer requires real TLS contact and zero HTTP
requests, native failed login without credential publication, duplicate request
identity/CAS rejection and retained terminal cancellation. The actual browser
adapter/shared client/controller also executes setup commands against that native
service. Synthetic DOM and VS Code API fixtures verify Settings controls,
ownership, HTTPS links, saved observation, lost-reply non-replay and view detach.
The first TLS-observation and VM import fixture failures are retained. Trusted
HTTPS positive OAuth login, registration selection, refresh/revocation, running
owner coordination, CLI login and current packaged/installed/rendered acceptance
remain unfinished. [Exact evidence](evidence/native-oauth-service-local.json),
[service and client boundary](native-mcp-login.md).

Native OAuth grant persistence/factory integration passes the complete
**112/112** local gate in **384.25 seconds**, with **2446** unchanged mapped inputs.
Actual xlang3 SQLite fixtures verify complete encrypted grants, a single-query
credential/metadata snapshot, atomic rotation/rollback/reopen, exact
resource/issuer/client/server binding, ciphertext relocation and malformed
record rejection, retired IDs and disabled-connector administration. The native
factory rejects missing/expired grants. An independent socket peer verifies one
valid discovery and zero later requests after expiry; the owner retires instead
of reconnecting/replaying. Authority digests change with OAuth credential
references/revisions. Refresh-token spaces survive native parsing and encrypted
storage. The first fixture-syntax build failure is retained; a final full build
includes all late source edits. Prior client source is unchanged and its 244/54
suites were not rerun here. Authenticated setup routes, trusted HTTPS login,
refresh/revocation, coordination of running owners and client login controls
remain unfinished. [Exact grant evidence](evidence/native-oauth-grants-local.json).

The native authorization-owner source `dd08604` passed the complete hosted
**110 native** gate in **332.01 seconds**, plus **244 extension / 54 browser**
contracts and packaging. Independent downloaded archive/VSIX verification checks
the **1860** runtime files, **29** source-bundle files, exact contract names and
packaged view sources. Original raw outputs and hashes are retained. This package
predates the newer callback receiver and encrypted OAuth grant integration and
has no installed/rendered current acceptance.
[Hosted authorization package](evidence/native-oauth-authorization-hosted-dd08604.json).

The native loopback callback receiver passes real-socket synthetic browser
redirects through the production authorization owner. The complete **111/111**
local gate passes in **378.16 seconds**, with **2443** unchanged mapped inputs.
A later two-file test-only strengthening and rebuilt callback contract passes
**12** cases in **0.99 seconds**, now checking port closure while the native
process/receiver remain alive. Production inputs/binaries remain unchanged;
the complete gate was not repeated after that fixture change. Exclusive Windows
binding, exact redirect/raw target/state/issuer admission, fixed non-reflecting
responses, denial, cancellation and deadline/owner expiry are verified within
that scope. Authenticated setup routes, encrypted OAuth grant persistence,
registration selection, trusted HTTPS login and client controls remain
unfinished. The first missing-include build failure is retained.
[Exact callback evidence](evidence/native-oauth-callback-local.json).

The configured native MCP HTTP and terminal-feed source `7007010` passed its
complete hosted **110-contract** native gate in **447.12 seconds**, plus **244
extension / 54 browser** contracts and packaging. Independent downloaded bundle
and VSIX verification checks all **1860** runtime files, the **29** source-bundle
files, exact contract names/gate hashes and packaged view bytes against that
commit. Original output and hash provenance are retained. This package predates
the newer authorization owner and callback listener and is not installed in the
running previews. [Hosted package evidence](evidence/native-mcp-http-hosted-7007010.json).

Native OAuth authorization components pass the complete **110/110** local gate
in **376.23 seconds**, with **2439** unchanged mapped inputs and the pinned native
xlang3 runtime, without executing CPython. The production private form encoder
is checked by an independent socket peer for exact resource/client/redirect/code
fields and RFC PKCE. Native fixtures cover random authorization requests,
callback state/issuer/redirect/expiry/replay, allowlisted denial, private token
parsing, cancellation and untrusted HTTPS rejection. Trusted HTTPS login,
callback listener/routes, registration selection, encrypted OAuth persistence,
refresh/revocation and browser/VS Code login remain unfinished. The earlier
244/54 client results apply to unchanged client source; those suites were not
rerun for this native-only checkpoint. Hosted and installed acceptance remain
separate. [Exact local component evidence](evidence/native-oauth-authorization-local.json),
[implementation boundary](native-mcp-http.md).

Native MCP HTTP/OAuth and terminal-feed checkpoint passed the complete local
**110-contract** gate in **371.74 seconds**, with **2438** unchanged mapped inputs,
the pinned native xlang3 runtime and no CPython interpreter/bridge. The complete
**244 extension / 54 browser** synthetic suites also pass. Source and binary
hashes, exact contract names and original detailed outputs are retained.
[Complete local acceptance](evidence/native-mcp-oauth-local.json).
The native configured HTTP owner/approval registry passes actual official SDK
JSON/SSE file effects, denial with zero dispatch, duplicate rejection, lost-reply
uncertainty and xlang3 SQLite restart. Native metadata/challenge fixtures,
separate Basic/Bearer response headers, typed 401 retirement, cancellation,
deadline and untrusted-TLS rejection pass. No positive OAuth HTTPS discovery,
registration/login/token exchange, refresh or graphical setup is delivered.
[Implementation and remaining scope](native-mcp-http.md).

The older hosted `298da66` view gate failed before the complete native suite:
after human input, no committed event arrived incrementally. Browser and VS Code
could stop their owned feed when a faster command snapshot already showed
completion. Both now retain the selected feed through its native terminal
acknowledgement. Original prior-source regressions reproduce both defects; all
**244 extension / 54 browser** synthetic contracts pass. Selection/disposal still
retires observation. The actual native view contract passes in **78.32 seconds**,
including real sidebar creation, exact approval/receipt/disk checks and terminal
stream cursor validation. This exercises the production extension/controller with
VS Code API fixtures; it is not rendered or installed IDE acceptance.
[View regression and hosted failure](evidence/native-browser-terminal-drain-candidate.json),
[native view follow-up](evidence/native-mcp-oauth-local-view-drain-recheck.log).

The first complete local native attempt passed **108/110** in **359.55 seconds**.
It retained a 60-second first-profile startup timeout and the sidebar race.
Fresh profile acceptance subsequently passed in **74.42 seconds** with no timeout
increase or native profile-code change. The original startup timing failure is
not attributed to a proven root cause. The sidebar check now waits for the actual
terminal protocol acknowledgement and exact final cursor before retaining its
independent incremental-event/history/effect assertions. The complete revised
gate subsequently passed as recorded above; the original startup timing failure
still has no proven root cause. Hosted packaging and installed/rendered current
coding acceptance remain pending. Earlier source-only statements below are
historical checkpoints superseded by this scoped local result.
[Original failed gate](evidence/native-mcp-oauth-local-first-failed.json),
[profile/view first follow-up](evidence/native-mcp-oauth-local-profile-view-first-recheck.log).

The later hosted `ad5a363` also failed the same early browser incremental-event
assertion before reaching its complete native gate. It predates both terminal
feed-retention fixes. Its original log/provenance are retained separately;
the current local result is not advertised as a hosted or installed pass.
[Hosted failure provenance](evidence/native-browser-terminal-drain-ad5-failure.json).

Native early view acceptance failed at `e6032d9`: its actual browser resume
reported `Unowned graph event`, so the full native gate was not reached. Original
hosted failure/provenance/job output is retained. The browser snapshot fetched
children before events; a newly committed child could be missing from that older
owner list. Source now reads events before fresh root/child metadata, and refreshes
terminal ownership/operations before displaying final events. Ownership rejection
remains intact. New deterministic initial/terminal fixtures reproduce both errors
against the original source and pass the correction; an unowned event still fails.
Full **243 extension / 53 browser** synthetic suites pass with all **2436** tracked
inputs and 12 generated assets unchanged. Actual native correction acceptance,
current MCP compilation and installed/rendered coding remain pending.
[Failure and candidate evidence](evidence/native-graph-snapshot-order-candidate.json).

Native MCP HTTP configuration/factory candidate: production agent execution,
idle context preparation, graph tools and admin discovery now select a shared
native owner from immutable stdio/HTTP settings. HTTP endpoint and encrypted
server bearer references bind credential purpose; native authority includes
transport, endpoint, reference and actual credential revision. Public server
metadata reports the transport without destinations/keys. Admin provisioning
uses the registered `BEARER` target and retains catalogue-reflection checks.
The official HTTP SDK effect assertion now goes through persisted configuration,
encrypted credential resolution and this production factory. Existing native
configuration/authority contracts add URL/mixed-field/plaintext/scope rejection,
endpoint-change binding, pre-connect cancellation/deadline and SQLite reopen.
Whitespace/source and Node fixture syntax checks pass; native acceptance remains
pending. OAuth, broader HTTP lifecycle and graphical setup remain unfinished.
[Current implementation boundary](native-mcp-http.md).

Native MCP HTTP owner candidate: `McpHttpClient` now implements the same private
dispatch interface as stdio, using incremental JSON/SSE decoding and shared tool
description/result validation. Negotiation, per-tool header filtering, immutable
catalogue checks, exact request correlation and interruption attribution are in
native source. The existing effect contract is expanded with a pinned official
HTTP SDK peer: real JSON/SSE disk effects after approval, denial with zero calls,
duplicate rejection, one lost-reply effect and actual SQLite restart checks.
The peer independently checks bytes/counts. Node syntax/whitespace and a separate
SDK-only schema/response-shape probe pass; C++ compilation and execution remain
pending. Product configuration/factory, OAuth and broader HTTP lifecycle remain
unfinished. [Exact scope](native-mcp-http.md).

Native MCP HTTP POST candidate: the native async WinHTTP layer now sends
request/schema-derived headers and exposes bounded response metadata/bytes
for JSON/SSE and authentication challenges, with a conservative send boundary.
Existing provider behavior retains its selected-header/media rules. Real-socket
synthetic peer assertions are added to the existing transport contract for raw
arguments, both media, status/session/auth metadata, acknowledgement shape,
exact dispatch counts, cancellation, deadlines and certificate rejection.
Node syntax/whitespace checks pass; native compilation/execution is pending.
Connected MCP ownership, decoding, configuration/factory and OAuth remain
unfinished. [Scope](native-mcp-http.md).

Native browser streaming accepted at `977e945`: the full hosted **110 native
contracts** passed in **356.85 seconds**, with **241 extension / 49 browser**
fixtures and actual browser/native integration. The production cookie client,
shared reader/subscription and native gateway now have actual paused
detach/reattach, checkpoint-bound human input, committed incremental events,
two-stream capacity/command fairness and process-exit revocation acceptance.
Independent verification checked the downloaded bundle/runtime/VSIX hashes,
all 16 host/view source mappings and generated browser assets. Later clean EOF,
gateway restart, actual extension controller and approved creation assertions
remain pending, as do fresh installed/rendered coding and live inference.
[Scope](evidence/native-event-stream-hosted-977e945-scope.json),
[package verification](evidence/native-event-stream-hosted-977e945-package.json).

Native MCP HTTP metadata candidate: the actual wire library now validates and
projects modern request headers and declared parameter headers, including
exact safe integers, nested paths, Unicode/control encoding, absent/null values
and case-insensitive name uniqueness. Its existing native contract has bounded
fixtures; compilation/execution is pending. HTTP networking, discovery filtering,
OAuth and effect acceptance remain unfinished. The design now distinguishes
modern request-scoped HTTP from older session/resumption semantics.
[Implementation and remaining acceptance](native-mcp-http.md).

Native MCP transport interface candidate: production `McpToolRegistry` now
accepts a transport-neutral `McpToolClient`, implemented by the existing stdio
binding. Dispatch stays private to the same durable approval/effect registry.
Common failures preserve possibly-sent/request/response attribution. The actual
MCP effect contract is updated to use the abstract interface and require private
dispatch/shared deadline/noncopyable ownership at compile time. Whitespace/source
checks pass; native compile/execution is pending. HTTP/OAuth remain unavailable.
[Pinned protocol, implementation boundary and remaining acceptance](native-mcp-http.md).

Production sidebar approved-creation acceptance prepared: a second actual
native graph requires human input, then a `create_file` proposal. The host
fixture runs the production comparison path and requires absence before
approval, rejection of a retired sidebar's decision, current-sidebar approval,
exact Unicode disk bytes and persisted receipt hash/size, one child and terminal
history. Syntax/whitespace checks pass; actual native execution is pending.
Model requests remain excluded from this acceptance; installed/rendered coding
and live inference are separate requirements. [Scope](native-event-stream.md).

Actual production VS Code controller acceptance prepared: the native view
contract now imports a host fixture that executes production `extension.js`,
`WorkspaceBackend`, client and subscription against the real compiled backend.
It requires paused replay, sidebar disposal/reopening, rejection of a retired
view's human input, current checkpoint-bound input, exactly one actual tool
child and persisted terminal history. Native transport is instrumented for
observation only; no domain replies are fabricated. Only syntax/whitespace
checks pass so far. Native execution and installed/rendered IDE acceptance
remain pending. [Acceptance scope](native-event-stream.md).

Clean frame-boundary EOF recovery candidate: the shared committed reader still
rejects a missing native end acknowledgement, but now marks a clean connection
EOF as transport unavailability. The subscription retries at the last accepted
cursor with its existing three-attempt bound; it sends only stream GETs.
Incomplete/malformed frames, identity/authentication and consumer failures stop
explicitly. Complete **243 extension / 50 browser** synthetic suites pass with
all 2421 tracked inputs unchanged. The real native view contract now also
prepares a browser-adapter restart at the same origin/cookie, requiring a fresh
validated observation at the preserved cursor while the actual graph stays
paused. That new native assertion is unexecuted.
[Candidate and original evidence](evidence/native-event-stream-eof-candidate.json).

Native SSE transport accepted at `7a148ff`: the isolated hosted build passed
all **110 native contracts** in **371.87 seconds**, then **224 extension /
43 browser** fixtures, actual browser/native integration and package checks.
The executed native view contract compares completed real tool-graph run,
graph and owned-tree streams with persisted REST records, verifies
Last-Event-ID replay/cursor rejection and the scoped browser-cookie feed.
Independent artifact verification checked all 29 bundle/1860 runtime file
digests, the exact native contract set, 16 packaged host/view source mappings,
generated browser HTML and vendor copies. Native source compiled; no local
native execution or installation change occurred. Production reader,
subscription/controller adoption and active-feed/capacity/revocation assertions
were added later and remain unverified natively. Fresh installed/rendered
streaming remains pending.
[Exact scope](evidence/native-event-stream-hosted-7a148ff-scope.json),
[verified package](evidence/native-event-stream-hosted-7a148ff-package.json).

Full webpage streaming fixture: the complete emitted classic-script order,
cookie connection bridge, actual shared reader/subscription and renderer now
run together in JSDOM. Synthetic committed frames render live text and exact
normalized usage, remain idle without periodic run polling, replace live output
with one persisted terminal response and detach without sending cancellation
or admission. Complete **241 extension / 50 browser** synthetic suites pass
with all 2421 tracked inputs unchanged. This is DOM fixture evidence, not a
native backend or installed/rendered browser acceptance claim.
[Scope and original evidence](evidence/native-event-stream-browser-page-candidate.json).

Browser cookie-stream correction candidate: the page's session client previously
overrode JSON requests only, so its inherited SSE method attempted bearer-token
acquisition and failed. `BrowserSessionClient` now supplies same-origin cookie
transport for both JSON and SSE while retaining the shared committed parser,
ownership checks and subscription. Editor clients retain protected bearer
transport. All **241 extension / 49 browser** synthetic checks pass with all
2421 tracked inputs unchanged. New fixtures exercise the production cookie
client and parser, UTF-8 delivery, no bearer acquisition/export, pre-abort,
invalid scope and expired-session failure. The real native controller acceptance
now uses that actual cookie client through the browser adapter; it remains
unexecuted. [Candidate and original evidence](evidence/native-event-stream-browser-cookie-candidate.json).

Actual native/browser-controller stream assertions are prepared in the native
view contract: a real human/tool graph stays paused during observation, survives
view disposal, replays after reattachment, accepts explicit checkpoint-bound
input and completes its actual file read. Assertions require incremental
committed frames, unique displayed sequences and exact persisted terminal
history. Only syntax/whitespace checks have run; this acceptance remains pending.
The separate local benchmark is still live, so no local native launch is claimed.
The same contract now prepares actual two-stream capacity/command-fairness,
reader detach and view-process-exit reauthentication checks while a real graph
remains paused. Those additional checks are also unexecuted.

VS Code subscription adoption candidate: the actual extension host now uses
the same scoped committed-event subscription as the browser. Periodic run
polling is removed. Event-triggered snapshots preserve history, approvals,
plans and owned children; graph child transcripts refresh after direct events.
Direct streamed delivery awaits the webview acknowledgement. Selection,
workspace/view changes and disposal retire observation without cancelling runs.
All **241 extension / 47 browser** synthetic checks pass with all 2421 tracked
inputs unchanged. New host fixtures cover delivery and coalesced metadata,
retired callbacks and graph child transcript refresh; historical-run selection
now follows the active feed. Native streaming and fresh installed/rendered
acceptance remain pending.
[Candidate and original evidence](evidence/native-event-stream-vscode-adoption-candidate.json).

Browser subscription adoption candidate: the browser controller now observes
the shared committed-event feed, with no periodic HTTP timer. Read-only
run/graph/approval/plan/history snapshots refresh on observation, committed
events and terminal end; event-triggered refreshes coalesce for 100 ms. Serialized
snapshots discover new child ownership after an older snapshot, and overlapping
REST/SSE records render once. Selection changes and disposal retire observation
without cancelling execution. All **238 extension / 47 browser** synthetic
checks pass with all 2421 tracked inputs unchanged. Actual native streaming,
VS Code adoption and fresh installed/rendered acceptance remain pending.
[Candidate and original evidence](evidence/native-event-stream-browser-adoption-candidate.json).

Shared subscription lifecycle candidate: connection rotation resumes the
delivered cursor, and root/conversation/origin/generation changes abort only the
old observation and refuse its delayed events/outcomes/retries. Busy/transport
retries are bounded; authentication, protocol and consumer failures stop
explicitly. All **238 extension / 43 browser** fixture checks pass with all
2421 tracked inputs unchanged. The real native contract now includes production
subscription replay, but it has not executed. Both UI controllers still poll;
adoption and installed/native acceptance remain pending. The accepted installation
is unchanged. [Candidate and exact verification scope](evidence/native-event-stream-subscription-candidate.json).

Shared event reader candidate: `BackendClient.eventStream` validates root,
conversation, scope, committed IDs and owned-child metadata, decodes split UTF-8
and CRLF, preserves consumer-acknowledged cursors and detaches on abort/protocol
failure without sending commands or fallback requests. All **232 extension /
43 browser** fixture checks pass with all 2421 tracked inputs unchanged.
Production-reader replay/resume checks are added to the real native view contract
but have not run yet. Native integration, subscription/controller adoption and
installed/rendered streaming remain pending; the accepted installation is
unchanged. [Candidate scope and original evidence](evidence/native-event-stream-client-candidate.json).

Committed-event transport candidate: native run/graph/owned-tree SSE endpoints,
scoped cursor replay, bounded connection/stream leases and incremental native
view/browser forwarding are implemented. The complete local **224 extension /
43 browser** fixture suites pass with all 2421 tracked native/tool/view inputs
unchanged. The browser fixture observes a frame before the peer finishes and
detaches without sending cancellation. New real native replay/cookie assertions
are prepared but unexecuted. Current UI controllers still poll; native acceptance,
shared client/controller adoption and installed/rendered streaming remain pending.
The accepted `689404e` installation is preserved.
[Protocol, implementation and remaining acceptance](native-event-stream.md).

Hosted source `689404e` passed **all 110 native contracts** in **275.04 seconds**,
then **224 extension / 41 browser** checks, actual browser/native integration,
runtime staging and VSIX verification. The real production editor host connected
through the native view, wrote sessions, ran a native tool graph, shared console
history, enrolled browser access and verified process-exit revocation and same
profile/history after host reload. This is actual native integration with a
fixture VS Code API, not installed/rendered editor acceptance or live inference.

Independent downloaded-artifact verification checked the exact registered and
passed contract sets and original gate hashes, all **29 bundle / 1860 runtime**
file digests, **16** committed host/view source mappings, generated browser HTML
and vendor copies. The VSIX has **1897 entries**, five native files and 1830 pure
source files. Original logs/manifests and the verifier's initial source-map error
are recorded. No local native execution or current installation acceptance is
claimed. Fresh installed/rendered coding, declarative/code authoring,
workers/IPC, profile binding and full parity remain required. Native/browser-
controller SSE acceptance now passes locally; installed/rendered acceptance
remains open. No migration layer is implemented.
[Passed gate, original evidence and independently verified package](evidence/native-view-hosted-689404e-package.json).

That exact VSIX is now installed into a fresh isolated VS Code profile. All
**1895 installed extension files** were compared with the verified archive;
only VS Code's added package `__metadata` is excluded from structural manifest
comparison. Every native runtime byte matches. Existing installations were
preserved. This is installation/file evidence only: no local native runtime was
launched while the separate benchmark parent remains live. Rendered interaction
and actual coding acceptance in this profile are still pending.
[Fresh installation and complete file comparison](evidence/native-view-hosted-689404e-fresh-install.json).

Hosted source `0b96974` compiled and reached native view readiness, then failed
the first actual session write with HTTP 400 `Invalid JSON request`. The owner
fix therefore passed that startup boundary, but the native contract did not pass.
Source inspection found that forwarding ran before cpp-httplib read POST bodies.
The candidate now keeps access checks in pre-routing and forwards from regular
GET/POST handlers. Exact Unicode/chunked writes and malformed/oversized rejection
are added to the real native contract. Syntax/whitespace checks pass; native
rerun, the full 110-contract gate and fresh installed UI acceptance remain pending.
No migration layer or local native execution was added.
[Original native failure and candidate scope](evidence/native-view-body-forwarding-candidate.json).

The corrected hosted build reached real view startup and failed its strict owner
check. Original runner provenance confirms that the default owner differs from
the current user. The host now chooses an unused name and leaves leaf creation
to native's explicit-user protected storage. All **224 extension / 41 browser**
fixture checks pass, including collision and alias refusal; the real native test
now checks that the host leaves the leaf absent before launch. Native rerun,
the full 110-contract gate and installed acceptance remain pending.
[Original failure, before-fix regression and candidate evidence](evidence/native-rendezvous-owner-candidate.json).
The same pushed source passed hosted **224 extension / 41 browser** checks with
all 44 source/vendor files and 12 generated assets unchanged. Downloaded asset
bytes match the original gate digests. This does not prove native startup.
[Original hosted gate and independent artifact checks](evidence/native-rendezvous-owner-hosted-views.json).
The older queued `da4f6e1` build then repeated the same owner failure; it does
not contain this host fix. The corrected `0b96974` native run has now started.
[Original older failure and corrected run identity](evidence/native-rendezvous-owner-older-confirmation.json).

The local native launcher now detects the actual Python benchmark parent as well
as its workers, and rechecks before configure, compile and contracts. The previous
guard missed live parent PID 8220; a real launch after the fix exited with deferral
code 3 before creating a build directory. The benchmark was not changed and no
native test pass is claimed. Isolated native CI continues independently.
[Actual parent observation and launcher refusal](evidence/native-benchmark-parent-guard-local.json).

Hosted source `faab16f` compiled and finished its full native test run with
**107/110 passed** in 310.95 seconds. The same license-staging and removed editor
upgrade-API failures blocked it; the actual local-view regression did not run.
Those causes were already corrected in `9f73bdc`. The corrected `645d76f` run
has started, with the real view test scheduled before unrelated test compilation.
Native readiness and the complete gate remain unaccepted.
[Original older result and exact current-run identity](evidence/native-view-preflight-hosted-failed.json).

The existing earlier browser checkpoint was inspected and its history refresh
button exercised without submitting a model request. Saved transcript/usage
and provider choices were visible, but refresh required connecting again.
Cached display is not proof of a usable backend connection or acceptance of
the current native adapter. No credential was entered or authentication bypassed.
[Actual browser observation and original screenshot](evidence/browser-existing-refresh-observation.json).

Protected profile creation now explicitly selects the current Windows user as
owner, matching the existing verification rule even when a token's default
owner is a group. The DACL and refusal of foreign-owned existing storage are
unchanged. Windows descriptor parsing confirms the supplied owner and unchanged
access entries; native compilation and execution remain pending. This is not
established as the earlier view-readiness failure's cause.
[Candidate source and scoped descriptor probe](evidence/native-profile-explicit-owner-candidate.json).

External attachment now rechecks workspace trust and host location after native
workspace verification, before accepting the connection. The regression failed
on the previous code; all **221 extension fixture checks** pass after the fix.
[Failure and complete rerun](evidence/view-attach-trust-local.json).
The same pushed source passed the complete hosted **221 extension / 41 browser**
gate with all 44 source/vendor files and 12 generated assets unchanged.
[Downloaded original evidence and independently checked asset digests](evidence/view-attach-trust-hosted.json).
This does not establish native or installed-editor acceptance.

The first hosted native-view gate completed with **107/110 passed**. The two
local profile/view contracts stopped during packaging because the pinned pure
source checkout has `LICENSE`, while the fixtures requested `LICENSE.txt`.
The handoff contract then invoked the removed editor upgrade API. The candidate
fix uses the actual source license, removes that obsolete editor scenario while
retaining native owner-handoff safety assertions, and invokes the browser test
through `xmind.exe serve`. No migration flow is introduced. Syntax checks pass;
the complete native rerun and installed acceptance remain pending.
[Original hosted failure and scoped fixes](evidence/native-ci-current-format-candidate.json).

First-sidebar reconnect fix: changing folders before the first view exists
now preserves the pending open intent and reopens the latest workspace. The
regression failed on the previous code and the complete **220 extension fixture
checks** pass after the fix. No run cancellation is issued.
[Original failure and complete rerun](evidence/first-sidebar-reconnect-local.json).
Native adapter/installed acceptance remains pending.

Editor startup selection fix: a folder change during pending preparation now
waits for stale cleanup and connects the latest root; trust revocation prevents
new native startup. The complete **219 extension fixture checks** pass.
[Scoped source and original output](evidence/view-startup-selection-race-local.json).
Native view readiness and installed acceptance remain pending.

Native editor-view adapter candidate: the host now delegates startup/storage
to C++ and scopes saved UI choices to the native workspace/profile. All **217
extension fixture / 41 browser fixture** checks pass. Native acceptance is
**pending**: the first probe timed out preparing the backend; the second started
it but the view failed before readiness. Diagnostic reporting is prepared.
Further native builds/probes were deferred for a separate live xlang3 performance
run. No working installed connection, native 110-test gate or UI rendering is
claimed. [Contract and remaining acceptance](native-view-adapter.md).

Native automatic console profiles now start or reconnect to one persistent
workspace backend without a manual port/token setup. The controller verifies
the actual native process and authenticated workspace, seals its rendezvous
with user DPAPI and preserves the same SQLite sessions/history after restart.
A console exit leaves its backend alive. The full **109 native contracts** and
**10 focused checks** pass with frozen source. Runtime inventories remain
immutable while ancestor control records can update atomically.
[Profile contract](local-profiles.md) · [Actual local evidence](evidence/native-local-profile-local.json).
This is native console acceptance; installed VS Code/browser controller adoption,
worker IPC, authoring and complete parity remain pending. No migration is needed.

Native console workspace pinning now accepts `--workspace DIR`, checks the
selected OS directory identity and authenticated backend authority before each
request, and sends the pinned pair in agent/graph admission. The actual native
server rejects a stale-authority race before creating a run. The complete local
**108 native contracts** and eight focused checks pass with frozen source.
The first focused test had an incorrect stderr assertion; its original log is
retained and the corrected test checks the native HTTP status and stored state.
[Console connection evidence](evidence/native-console-workspace-local.json).
Automatic profile discovery/start was pending at that checkpoint; the native
controller is accepted above.

The verified **0.1.5** VSIX is installed in a fresh dedicated VS Code profile;
its runtime and 25 client/browser assets match their accepted hashes. First-run
sign-in is awaiting user handling. No fresh rendered-sidebar or installed-model
acceptance is claimed. The older installation is intact.
[Installed file evidence](evidence/vscode-unified-fresh-installed-files.json).

The single-format package checkpoint **6adb9a1** removes the client legacy-profile
migration path and accepts only `xmind.exe` plus xlang3 dependencies. The complete
local **107 native / 225 extension / 41 browser** suites pass. The first native
attempt exposed an outdated PowerShell test build path; the corrected complete
rerun passed in **210.08 seconds**, with **2,414 frozen inputs**.
[Source and original logs](evidence/native-unified-package-local.json).

The new **0.1.5 VSIX** passed exact verification of **1,935 archive entries**,
including **1,869 pure-source files** and **23 license files**. Its actual packaged
`xmind.exe` completed two real OpenAI runs in a fresh SQLite profile: five
approved effects, one denied effect, actual provider usage and exact multiline
history across restart. No old profile or installation was touched.
[Package and live evidence](evidence/native-unified-package-release.json).
Fresh installed/rendered VS Code acceptance, declarative/code authoring APIs
and agent-worker integration remain pending. Native/browser-controller SSE
integration passes locally; rendered/installed UI acceptance remains open.
Native console bootstrap is accepted
in the later checkpoint above.

The primary **`xmind.exe`** now executes the existing native backend, console,
admin and private schema handlers through one program. The complete local
**107-contract gate passed in 233.55 seconds** with **2,393 unchanged mapped
inputs**. The four unified contracts cover actual patch approvals/effects,
graph execution, MCP subprocess effects, Unicode configuration paths and
embedded-xlang3 SQLite restart. Synthetic provider fixtures remain explicitly
scoped. The first focused attempt exposed a Windows Unicode config-open defect;
all eight admin/server readers were corrected and the original failure retained.
[Complete native evidence](evidence/native-unified-local.json).

The exact tested `xmind.exe` hash also completed **two real OpenAI gpt-6.1-sol
console runs**: literal multiline mixed add/update/move/delete and a partial
two-file patch with the second file denied. Five actual approved effects and
one denied effect matched native receipts and disk hashes. Both prompt/history
records survived an actual backend restart unchanged, and six actual provider
usage records were retained. [Live console evidence](evidence/native-unified-cli-live.json).

The pinned xlang3 SDK independently passed its official shared-memory script,
parallel-client and native C++ smoke without a rebuild or CPython. This is a
transport prerequisite, not integrated agent-worker or performance acceptance.
[IPC evidence](evidence/xlang3-ipc-foundation-local.json) and
[worker design](native-ipc-worker-design.md).

The installed client still uses its existing 0.1.4 runtime generation. The new
package uses one `xmind.exe` and will be validated with a fresh local profile;
legacy-profile migration is excluded by the product decision. Fresh installed
acceptance and automatic console bootstrap remain pending, along with
declarative/code authoring APIs and agent workers. Native/browser-controller
SSE integration now passes locally; rendered/installed UI acceptance remains
open.

The CLI multiline/composer source checkpoint passed the complete **103 native
contracts in 215.71 seconds**, with 2,390 unchanged mapped inputs, plus **222
extension / 41 browser** fixture tests. Native contracts exercise composed
agent/graph prompts and discard boundaries; provider responses are synthetic.
The first complete attempt also passed 103 tests but its source-freeze wrapper
correctly rejected a concurrent extension version change; a fresh gate passed.
That earlier source-only gate did not establish installed 0.1.5 rendering or
live-provider multiline acceptance; the later live console evidence above is
separate. Installed 0.1.5 rendering remains pending.
[Frozen evidence](evidence/native-compose-local.json).

The next product direction is [Agent Runtime + Coding Harness + Model
Gateway](runtime-product-design.md), with one primary `xmind` executable. The
document distinguishes existing native execution from pending authoring,
streaming, bootstrap and worker delivery.

The audited **0.1.4 package** now runs in the existing TestProj VS Code profile,
preserving all 22 messages and seven runs through the native upgrade. An actual
OpenAI two-file patch opened comparison before effects, required two separate
UI approvals and completed with matching native receipts, disk hashes and
provider metrics. The packaged interactive CLI separately passed real mixed
add/update/move/delete and partial denial, plus exact restart history. The
archive audit checked 1,938 entries and 1,901 runtime files; the versioned
client suites passed 222/41. The installed 0.1.4 composer and CLI retain their
older behavior; the source fixes above have not been installed there. Older
browser-preview coverage and full coding parity remain incomplete.
[Package and live/installed evidence](native-file-patch.md).

Native multi-file patch execution now passes the complete **103-contract local
gate** in 211.24 seconds after correcting the Windows handle-relative rename
call and a missing synthetic routing asset. All 2,390 mapped source inputs
remained unchanged. Separate hosted client acceptance passed **222/41** tests
with verified artifact/source bindings. The installed VS Code 0.1.3 client also
completed a fresh real OpenAI workspace read, preserving the earlier failed
conversation. This does not install or accept the new patch runtime; packaging,
rendered patch approvals and real-provider patch execution remain pending.
[Native evidence and limits](native-file-patch.md), [client evidence](patch-review.md),
[installed fresh-read recovery](evidence/vscode-sidebar-read-live.json).

Native discovery/content search now share hierarchical local ignore rules,
scoped Git repository boundaries, negation, source priority and explicit positive
glob overrides. All **98 native contracts passed in 209.78 seconds**, with 637
unchanged inputs, plus **215/39 client** contracts. A real OpenAI search returned
only two allowed paths and retained history across an xlang3/SQLite restart,
without mutation operations. Actual Git corpus decisions match; the first
ripgrep comparison exposed two retained dialect differences. Global ignore
configuration, full dialect/search parity and installed writing remain incomplete.
[Behavior and scope](native-ignore.md), [exact evidence](evidence/native-ignore-local.json).

Native `glob_files` supports recursive wildcard, brace, character-class and
Unicode path discovery through retained workspace handles. Ordinary agents,
read-only delegated children and direct graph tools share the implementation.
The corrected source passed **98 native contracts in 208.70 seconds**, with
635 frozen inputs, plus the unchanged **215/39 client** gate. Actual OpenAI
discovery found and quoted four fixture paths, excluded private configuration
and preserved history across an xlang3/SQLite restart. An actual depth-boundary
fixture passed after moving the enumeration buffer out of recursive frames.
The initial four stale synthetic-catalogue failures are retained. Gitignore
rules, escaped metacharacters and full search/editor parity remain incomplete.
[Behavior and limits](native-glob.md), [exact evidence](evidence/native-glob-local.json).

Native text-file pages support 1-based offsets, up to 2,000 lines, explicit
Unicode clipping and resumable output bounds for files up to 64 MiB. All
**98 native contracts passed in 207.52 seconds**, with 633 mapped inputs
unchanged, alongside **215 extension / 39 browser** contracts. A real
`gpt-6.1-sol` run read the exact final two lines of a 4.29 MB file, quoted their
Unicode text and retained the same history after an xlang3/SQLite restart.
No mutation operations were created. The initial live harness setup failure is
retained; full read parity and installed/editor writing remain incomplete.
[Behavior and limits](native-file-pages.md), [exact local evidence](evidence/native-file-pages-local.json).

Workspace-bound editor selection now appends exact, labelled buffer context
to the existing sidebar draft without a question popup or automatic submission.
Initial view readiness, stale editor/workspace state and retained-view
reconnection have accepted source contracts. All **215/39 view tests** passed;
actual filesystem metadata checks and the **0.1.3 VSIX** audit passed separately.
The native runtime was unchanged at this checkpoint. Installed/rendered
acceptance remains pending for a fresh current-format installation; no migration
is required.
[Behavior, artifact and limits](vscode-editor-selection.md).

The local **0.1.2 VSIX** packages the accepted native file/folder-creation and
Responses-diagnostic source. Complete archive/inventory and ten host/view
source checks passed, as did 200/39 versioned client tests and a qualified
packaged-server/xlang3 SQLite/session/retirement smoke. It contains no private
state or CPython executable/extension/bytecode. The existing installed profile
and pending migration were not replaced; installed writing remains unverified.
[Artifact, hashes and scope](native-parent-package.md).

Native file creation now supports approval-backed missing parent folders,
with existing ancestor guidance, disclosed directory effects and create-new
checks. Real OpenAI CLI creation/editing and both edit/creation denial passed;
denied creation left its folders absent. The final **98 native / 200 extension
/ 39 browser** gates passed. The stale committed CI view count was corrected,
and earlier attempts remain retained. Installed/rendered folder approval
acceptance remains separate.
[Implementation, live receipts and limits](native-parent-creation.md).

Real OpenAI native CLI file creation, editing and denial passed with actual
approval operations, filesystem hashes and six supplied response-usage events.
Responses failures now retain explicit native classifications and validated
usage, with actionable shared client messages. All **98 native, 199 extension
and 39 browser tests passed**. The earlier unclassified provider failure and
corrected acceptance-driver errors are retained. Installed VS Code writing
and its migration remain separate and unverified.
[Implementation, live evidence and limits](native-responses-failures.md).

Native legacy publication failure after real termination now has accepted
rollback/recovery coverage and clearer native/client diagnostics. All
**98/198/39 local tests passed**. Actual failure, absent ticket, retained session
and a separate operator preparation/activation were verified; rollback-call
failure and installed writing remain unverified. The installed migration dialog
still awaits the user's confirmation.
[Fault scope and evidence](native-legacy-publication-fault.md).

The verified **0.1.1** migration VSIX is installed in the existing normal
TestProj profile. Fresh 98/198/39 gates, independent archive audit and actual
complete-runtime smoke passed. Existing transcript/model/usage reconnected
without another key prompt. The old runtime was repaired after VS Code cleanup
using its verified copy. This historical migration package is superseded by
the current-format fresh installation; no migration confirmation is required.
Actual create/edit acceptance remains pending.
[Package and installed evidence](native-legacy-package.md).

Legacy migration now has native writer-fenced operator termination and managed
extension adoption/recovery. Complete local checks passed **98 native, 198
extension and 39 browser tests**. Actual server/admin/adapter fixtures retain
the same database and saved sessions, consume the exact ticket on activation,
recover lost stop output without replay and reconnect on host reload. Installed
migration and rendered coding remain pending.
[Implementation, evidence and limits](native-legacy-stop.md).

Native legacy preflight now authenticates the workspace and verifies actual
listener PID/birth, executable/hash and database/workspace command line before
migration. All **98 contracts passed**, with 631 mapped inputs unchanged.
It leaves the source running and creates no ticket. Installed shutdown/adoption
and rendered file writing remain pending.
[Exact scope and evidence](native-legacy-preflight.md).

Stopped legacy-owner preparation and qualified activation passed all **98 native
contracts**, with 629 mapped inputs unchanged. Actual administration/server
fixtures preserve the same database and keep admission closed until activation.
The native SQLite import repair addresses the retained hosted handoff failure.
View sources are unchanged from the previous 197/39 passing checkpoint.
Product legacy shutdown, installed upgrade and rendered file-writing acceptance
remain pending. [Scope and exact evidence](native-legacy-owner.md).

Provider/conversation switching now has catalogue recovery in both adapters,
with source-bound hosted **175/35** acceptance. Tests cover retired native
selection acknowledgements, a second switch during recovery and the exact
revision-bound submission fields. The tested browser asset is published;
rendered recovery and updated VSIX installation await local validation after
the independently running SDK benchmark clears.
[Recovery scope and retained failures](model-catalogue-recovery.md).

Compact-height browser/IDE styles now preserve conversation space. The actual
319 × 431 pane gained 96 pixels of history space; long drafts and expanded
context controls scroll without collapsing it. Complete 173/33 frontend suites,
independent VSIX verification and persistent-profile installation passed.
Native source and runs stayed unchanged. Rendered IDE interaction and the
separate provider/session catalogue recovery issue remain pending.
[Compact layout evidence](compact-sidebar.md).

The local browser preview now runs source `60475f8` with its matching assets
and paired xlang3 SDK. The upgrade preserved existing API records, configuration
and browser authentication. A disposable schema 10 → 12 → 10 fixture and fresh
model-free native/browser integration passed. All four configured providers
completed real rendered sidebar runs with supplied metrics. Refresh retained
the session/provider/model without reconnecting or admitting another run.
Desktop right-sidebar layout was checked; compact-height rendering and normal
VS Code rendered interaction remain pending.
[Browser upgrade, live UI evidence and limits](browser-preview-upgrade.md).

The native OpenAI model eligibility policy passed **89/89 contracts in 172.73
seconds**, with all 564 inputs unchanged. Discovery, enrollment and startup now
use explicit route/model declarations; current GPT-6 tool restrictions and
reasoning requirements are enforced before publication or execution. The 39
frontend inputs are unchanged from the 173/33 checkpoint. The subsequent native
live check passed all four configured providers, including GPT-6.1 Sol, and
OpenAI discovery returned 20 eligible IDs. The new package passed ZIP integrity
and actual installed VS Code opened-folder binding, then was installed in the
normal profile. Rendered VS Code interaction remains pending; the subsequent
browser upgrade and actual sidebar runs are recorded above.
[Model eligibility scope](native-model-eligibility.md).

The latest native provider checkpoint passed real workspace reads and exact
replies from OpenAI, Claude, Gemini and DeepSeek using the existing single YAML
configuration. The Claude direct-caller protocol fix passed 88 native tests;
saved-provider UI guidance passed 173 extension and 33 browser tests. The new
package passed ZIP integrity and actual VS Code opened-folder binding and was
installed in the normal profile, which needs a window reload. The older browser
preview and broad model eligibility remain pending.
[Current scope and evidence](native-provider-workspace-acceptance.md).

VS Code opened-folder checkpoint: the packaged extension passed actual host API
and authenticated Native identity checks on `D:\CantorAI2026\TestProj`, with no
`.code-workspace` file or manually entered server token. All 88 native contracts
passed after correcting Windows long lock paths; 171 extension and 32 browser
contracts also passed. The normal persistent window was installed and opened
with its trust settings unchanged and awaits the user's folder-trust choice.
[Scope and evidence](vscode-opened-workspace.md). No new screenshot or live model
inference is claimed for this checkpoint.

Current schema-v12 context, YAML import and DeepSeek source passed a complete
local gate. The first full 86-contract attempt passed **81 and failed five**;
the second full 88-contract attempt passed **85 and failed three in 162.98
seconds**, after configuration and compilation passed. After repository,
context-controller and CLI route fixture repairs, the third full gate passed
**88/88 in 161.48 seconds**, with zero failures/skips, 560 frozen inputs unchanged
and original test output captured. The failed attempts are retained without
exclusions. DeepSeek's synthetic gateway/profile contracts passed
individually in **0.54 / 0.71 seconds**; **131 extension / 29 browser** source
tests remain separate. The later Responses legacy-history serializer repair
passed the fourth complete **88/88 gate in 171.89 seconds**, with all 560 inputs
unchanged and no failures/skips. The final saved-provider footer and catalogue
retention changes passed **141 extension and 32 browser tests**, with all 37
frontend/vendor inputs unchanged. The later model-free native/browser
integration passed in **3.30 seconds** and the actual VSIX passed **18 asset
checks**, with all 12 tested view files matching and all 30 payloads hashed.
[Integration and package evidence](evidence/native-provider-footer-integration-provenance.json)
retains the initial guard-host failure. Hosted validation, installation and
live provider acceptance remain separate.
[Source-bound native gate](evidence/native-context-provider-local-provenance.json),
[Current context scope](native-context-compaction-design.md),
[provider scope](provider-setup.md).

The installed preview remains ace/schema v10 with one OpenAI Responses profile
and four routes; it has not imported the local four-key configuration and does
not advertise DeepSeek. An earlier request failed with HTTP 400 `invalid_value`;
an isolated direct reproduction later identified assistant `input_text` instead
of `output_text` in ace and current source. A fresh native OpenAI browser prompt
separately returned `OK` with supplied usage **3884 / 5**, without exercising
legacy replay or compaction. Direct account catalogue and small generation
checks passed for all four providers; native four-provider coding, installed
new-source clients and live compaction acceptance remain separate.

The latest accepted transport-only source `859aca7743e561e46fd574c82dff65df333a7bf4`
passed hosted **78 native contracts in 246.07 seconds**, **119 extension tests in
3.4198188 seconds** and **26 browser tests in 2.0534379 seconds**, all 16 steps,
model-free browser/native integration and 18 VSIX assets. Its immutable source,
archive payloads and original logs were verified. This predates the current
context/YAML/DeepSeek implementation and establishes no compaction or new preview
installation. [Exact hosted transport evidence](evidence/native-context-transport-hosted-859-provenance.json).

Exact source `3fc420480f61db75d3efaf4bda77fbefd8246166` subsequently passed its [hosted gate](https://github.com/xlang-foundation/xMind/actions/runs/37808089507): **77 native contracts in 206.89 seconds**, **119 extension tests in 2.4612639 seconds** and **26 browser tests in 1.3989153 seconds**. All 16 job steps passed, with the exact complete native manifest, zero failures/skips, model-free browser/native integration and 18 verified VSIX assets. Original logs, advertised archive digests and exact source/runtime maps were verified. Provider replies remain synthetic; this establishes the source checkpoint rather than installed or live planning acceptance. The installed preview remains ace/schema v10. [Artifact-bound hosted evidence](evidence/native-dynamic-plan-hosted-provenance.json).

Current completion scope is the [revised xMind OSS specification](architecture.md). Team-server features, PostgreSQL, WebRTC and the standalone Electron IDE are excluded and reserved for Nexus. Historical checkpoint paragraphs below preserve their original source/time scope and do not reinstate those requirements.

## Current checkpoint scope

The initial native dependency-planning checkpoint (schema v11) passed the complete guarded local **77-contract gate in 153.88 seconds**, with the exact expected/registered/passed manifest and all **421 frozen native source hashes** verified. Eligible ordinary Agents select agent/human dependencies, revise undispatched work and retain the same owner through clean human pause/reopen. Actual coding-child edits require separate exact approvals and can feed dependent verification. **119 extension and 26 browser contracts** passed at their source renderer/controller scope. Provider replies/signatures/keys are synthetic; native engines, files, permissions and embedded-xlang3 SQLite are real. The first **68/77** and second **76/77** attempts remain recorded failures. [Initial planning scope](native-dynamic-plan.md), [local source and gate evidence](evidence/native-dynamic-plan-local-provenance.json).

Model-free browser/native graph/human/auth/cookie/reconnect integration passed against the rebuilt server and matching assets; all **18 required VSIX assets** were packaged and verified. These checks establish neither rendered dynamic-planning nor installed-editor acceptance. The installed **ace2460/schema-v10** preview and its live read-only join remain separate. Schema-v11 installation, live planning and installed VS Code acceptance remain pending. Dynamic deterministic tool/MCP/process nodes, recursive planning, skills, compaction, outbound A2A and full coding/provider parity remain incomplete; private Nexus features stay outside OSS.

Native Responses reasoning continuation passed the exact ace2460 hosted gate: **71 native contracts in 171.52 seconds, 103 extension and 20 browser contracts**, with all 16 job steps, native/browser integration and 18 VSIX assets passing. Verified sources and artifact maps bind the completed-item continuation fix and its real native two-child read/join with synthetic provider replies. The local build was deferred by the benchmark guard. The exact native bundle is installed. One real OpenAI browser prompt completed two read-only native children and a joined parent response, with six owned model attempts, separate per-response metrics and zero effect operations. Immediate refresh retained the connection and selection. Upgrade and rollback checks passed at their separately documented scopes. [Compatibility and scope](native-responses-reasoning.md), [exact hosted evidence](evidence/native-responses-reasoning-hosted-provenance.json).

Ordinary Agent mode now offers native `delegate_tasks` for model-selected read-only workspace investigations. Actual child AgentRunners keep separate conversations and measured response usage/timing, share their parent's call/deadline limits, and return observed results before the parent continues or requests an approval-controlled effect. Schema v10 durably binds tasks, immutable presets, budgets and once-only settlements; recovery never replays children. CLI, browser and VS Code observe the same owned child histories and committed tree cursors.

For the preceding delegation checkpoint, the final guarded local **71-contract native gate passed in 142.36 seconds**, with an exact expected/registered/passed manifest, **45 frozen source hashes**, zero failures/skips and no exclusions. The delegation engine contract took **1.96 seconds**, actual HTTP/CLI/shared-controller acceptance **1.03 seconds**. **103 extension tests** and **19 browser tests**, fresh source-matched browser/native integration and 18-asset VSIX verification also passed. Provider replies, signatures and keys are explicitly synthetic; native agents, filesystem effects, permissions, xlang3 SQLite and transport are real. The [initial process-fixture positional-schema failure](evidence/native-delegation-initial-ctest.log) is retained separately; the corrected frozen source passed all 71 contracts.

Exact revision `e353a37799530a234a6fa13e51f61a5c52d3ae6a` subsequently passed its [hosted gate](https://github.com/xlang-foundation/xMind/actions/runs/37765666399): **71 native contracts in 175.51 seconds**, **103 extension tests in 2.9668103 seconds** and **19 browser tests in 1.426115 seconds**, plus native/browser integration and verification of all 18 required VSIX assets. The delegation engine took **4.88 seconds** and its HTTP/CLI contract **1.22 seconds**. All 16 job steps succeeded, with zero failures/skips and the exact complete native manifest. Downloaded artifact digests, exact source and runtime/stdlib pins were verified. [Exact hosted71 provenance](evidence/native-delegation-hosted-provenance.json), [raw hosted CTest](evidence/native-delegation-hosted-ctest.log), [original hosted job](evidence/native-delegation-passing-ci-job.log).

The preceding managed backend upgrade installed the exact hosted e353 bundle with schema v10; the later 7fe upgrade is recorded in the diagnostic scope. The actual upgrade preserved existing records, settings and browser access; its disposable schema 9→10→9 rollback fixture passed separately. The original e353 webpage exposed a classic-script name collision. A separate local browser repair passed **103 extension tests in 1.6557025 seconds**, **20 browser tests in 0.9090882 seconds**, native/browser integration and all 18 VSIX assets. The repaired page reused its cookie without key entry and restored prior history/metrics, the selected model and Agent mode. These frontend results are separate from the unchanged hosted 71/103/19 gate. [Upgrade evidence](evidence/native-delegation-upgrade-provenance.json), [acceptance scopes](native-delegation-acceptance.md).

The first e353 live `delegate_tasks` request failed with `responses_terminal_mismatch` before child admission: zero children, only the user history row and one parent model-budget attempt. That historical failure remains recorded; the later ace browser two-child join passed its separate acceptance above. Installed VS Code delegation acceptance remains unverified. The initial v11 planning source checkpoint is distinct from those installed/live results; broader planning, skills, compaction, outbound A2A and full provider/coding parity remain incomplete. [Implementation and limits](native-delegation.md), [broader native planning design](native-dynamic-plan-design.md).

The preceding direct MCP graph checkpoint adds model-free registered tool nodes
using the real native schema, controller approval and operation journal. Each
node pins a server revision and registry alias; fresh discovery must match before
proposal or dispatch. Exact raw literals survive inside `arguments_json`
strings, while inexact typed dependency numbers reject before child admission.
Root deadlines bound discovery/waits/dispatch. Actual acknowledged oversized
outputs retain succeeded operations; uncertain or unrecorded effects preserve
quarantine/fail-stop ownership without replay. Metadata/admission never start
peers, and stale human pauses remain inspectable/cancellable with resume rejected
before input commit. The stopped-backend native administrator supports private
credential discovery and catalogue reflection rejection.
[Direct MCP graph scope](native-graph-mcp.md).

The final complete local **69-contract native gate passed in 121.22 seconds**,
with the exact expected/registered/passed manifest, zero failures/skips and no
post-build exclusions. All **14 frozen source hashes** match the final pre-build
state. The new native graph/permission contract passed in **5.59 seconds**;
actual administrator/server/CLI/shared-view-adapter integration in **2.31
seconds**. Owned peers perform real approved file effects, with dependent native
reads, human pause/reopen, denied/cancelled/invalid/drift/stale paths, serialized
shared-server claims, lost replies, acknowledged output limits and injected
journal fail-stop/recovery. Peer metadata/protocol replies are synthetic. Fresh
browser/native integration passed the rebuilt server and source-matched assets.
Its first HTTP launch failure exposed an obsolete model-only guard; the [failed
interim log](evidence/native-graph-mcp-initial-ctest.log) is retained separately
from the final passing corrected-source gate and additional invalid model
configuration cases. Exact revision
`c4ec09fc3ee25c1b3a2bd9087b25f5af420ee616` subsequently passed its
[hosted gate](https://github.com/xlang-foundation/xMind/actions/runs/37755382937):
**69 native contracts in 184.36 seconds**, **98 extension tests in 3.2107402
seconds**, **17 browser tests in 1.5543002 seconds**, native/browser integration
and 18-asset VSIX verification. All 16 job steps passed, with an exact native
manifest and zero failures/skips. Live direct-MCP graph, rendered IDE and
complete parity acceptance remain incomplete.
[Exact local69 evidence](evidence/native-graph-mcp-local-provenance.json),
[exact hosted69 evidence](evidence/native-graph-mcp-hosted-provenance.json).

The [delegation design](native-delegation-design.md) now has the initial local/hosted-verified
implementation and transactional v10 migration described above. Broader dynamic
plans and live/client release acceptance remain required.

The preceding `fceb50b` Claude receipt checkpoint passed the full local **67-contract native
gate in 117.65 seconds**, with an exact manifest, zero failures/skips and no
post-build exclusions. The new history helper passed in **0.50 seconds**; the
expanded actual AgentRunner contract passed in **0.57 seconds**. Ordered
text/tool/thinking/redacted blocks, exact initial/fragmented tool-input JSON,
precise number tokens and escaped keys survive validated receipt persistence
and raw outgoing Messages replay. Two real file reads execute through
interleaved signature-bearing blocks; encrypted credentials and ordered history
survive xlang3 SQLite close/reopen without repeating tools. Unsigned blocks fail
before effects, while previous protocol failure, cancellation/recovery and SQL
conversation rollback cases still pass.

Hidden thinking/signatures/redacted data remain continuation material rather
than ordinary text output. Native matching/provenance checks and request,
receipt, message-count, depth and aggregate/role-merge bounds passed: 8 MiB
content/envelope and final request limits, 64 content blocks, 1 MiB/depth16 tool
input and 65,536-byte signatures. Fresh browser/native integration passed the
rebuilt server and matching assets. Frontend source equality with `ae7c4ba`
retains its prior **98 extension and 17 browser tests**, without a new frontend
rerun. Provider replies/keys/signatures are synthetic; local cryptographic
verification is not performed. Thinking/reasoning request controls, live Claude
acceptance, foreign signed/tool conversion, rendered IDE acceptance, preview
upgrades and complete parity remain incomplete. Exact revision
`fceb50ba0493f645ee5f0a00d5400e0f103df231` subsequently passed its
[hosted gate](https://github.com/xlang-foundation/xMind/actions/runs/37752689263):
**67 native contracts in 120.18 seconds**, **98 extension tests in 2.0700124
seconds** and **17 browser tests in 0.9230928 seconds**, native/browser integration
and verification of an 18-asset VSIX, with zero failures/skips and the exact
manifest. Hosted history/agent contracts took **0.31/1.03 seconds**. The hosted67
gate excludes the current direct MCP graph, administrator and server startup
changes.
[Local67 evidence](evidence/native-anthropic-history-local-provenance.json),
[exact hosted67 evidence](evidence/native-anthropic-history-hosted-provenance.json),
[receipt scope and bounds](native-claude-history.md).

The preceding `ae7c4ba` Claude checkpoint passed the full local **66-contract native gate
in 114.49 seconds**, with an exact manifest, zero failures/skips and no post-build
exclusions. Its new actual AgentRunner contract took **0.56 seconds**, verifying
two native file reads, correlated tool-use/results, encrypted credential reuse,
xlang3 SQLite history/reopen without repeated tools, held-stream cancellation
and recovery, and second-tool-row SQL rollback. Its normalized usage adapter
contract passed in **0.23 seconds**. Supplied uncached input/output/cache counters
and zeros remain distinct in events and persisted responses, without sums or
invented totals.

Frontend suites passed **98 extension and 17 browser tests**, with zero
failures/skips and actual shared-renderer DOM metric fixtures. Fresh browser/native
integration passed against that rebuilt server and matching assets. Exact
revision `ae7c4ba07d59f57cbe393eff5a01b7d5186dfbc5` subsequently passed its
[hosted gate](https://github.com/xlang-foundation/xMind/actions/runs/37748603655):
**66 native contracts in 131.92 seconds**, **98 extension and 17 browser tests**,
native/browser integration and VSIX verification, with zero failures/skips and
an exact manifest. Its Claude agent contract passed in **1.41 seconds**.
Both prior gates exclude the newer ordered signed/redacted receipt work above;
this is separate from hosted67 verification, CLI65 and hosted enrollment64.
Live Claude inference, rendered Claude IDE acceptance, the 21 pinned models and
full parity remain incomplete. Installed previews are unchanged.
[Local66 source/runtime/log evidence](evidence/native-anthropic-agent-local-provenance.json),
[exact hosted66 evidence](evidence/native-anthropic-agent-hosted-provenance.json),
[Claude scope](native-claude-agent.md).

The user-requested native-only cleanup was committed and pushed as
`dea588874e5a8c8e40bf1a158fa925520443c8a0`. The Python agent prototype and
old xlang Core/service/plugin assets are removed, root CMake delegates to
`Native/`, and maintained documentation is merged into `doc/`. Its hosted gate
passed **58 native, 88 extension and 17 browser contracts**, native/browser
integration and VSIX verification, with no failures/skips.
[Exact cleanup evidence](evidence/native-cleanup-hosted-provenance.json).

Gemini transport/replay source
`ddd1d3da087f9ca7f8b0b8d81705c82632e15094` passed a separate hosted gate:
**60 native, 88 extension and 17 browser contracts**, native/browser integration
and VSIX verification, with no failures/skips. This verifies native request,
SSE/HTTP transport and signed replay DTOs with synthetic socket fixtures.
[Exact transport evidence](evidence/native-gemini-transport-hosted-provenance.json).

The common gateway/history source
`75f45f0a036f1ffbab8c4b157df364f3697b52a0` passed its exact hosted gate:
**61 native, 91 extension and 17 browser contracts**, native/browser integration
and VSIX verification, with zero failures/skips. Its native gate took **121.84
seconds** and includes the stronger callbacks and escaped large-response cases
excluded from the earlier historical local 61 result.
[Exact hosted evidence](evidence/native-gemini-history-hosted-provenance.json).

The agent source `c9591fe79cad9a4253ac8088933f0c8a2848ded1` passed **62 native
contracts locally in 100.44 seconds**. Its real native agent/file reads,
xlang3 SQLite reopen and signed replay do not repeat prior tools. Provider
replies are synthetic. Browser/native integration passed again against its
compiled server, and its unchanged thin clients matched the earlier 91/17 pass.
[Local agent evidence](evidence/native-gemini-agent-local-provenance.json).
The exact hosted gate also passed **62 native, 91 extension and 17 browser
contracts**, native/browser integration and VSIX verification, with zero
failures/skips. Its native gate took **128.80 seconds** and excludes newer
catalogue/enrollment source.
[Exact hosted agent evidence](evidence/native-gemini-agent-hosted-provenance.json).

Enrollment source `2f5e0f05e9bfadcf86b5508863da0ec5a9e78cfe` passed its
exact hosted **64 native, 94 extension and 17 browser contracts**, native/browser
integration and VSIX verification, with zero failures/skips. The native gate
took **158.09 seconds**, following the local 64-contract pass in **120.98
seconds**. Authenticated Gemini pagination/full resources, encrypted enrollment,
owned-key file execution, cancellation/CAS failure, stale discovery and signed
xlang3 SQLite replay passed with synthetic replies, keeping generation methods
separate from backend tool policy. Direct-SQLite removal and shared `records.hpp`
cleanup are included.
[Exact hosted enrollment evidence](evidence/native-gemini-enrollment-hosted-provenance.json).

The preceding CLI checkpoint passed all **65 native contracts locally in 113.16
seconds**, with the exact manifest, zero failures/skips and no post-build
exclusions. Its new CLI contract took **4.23 seconds** and covers OpenAI, Claude
and Gemini profile catalogue/setup/selection, private environment input,
all-family public identity reflection rejection, safe provider diagnostics,
signed Gemini text/history/usage and actual SQLite reopen with encrypted credentials. Stale
discovery/admission does not retry or silently rebind; failed-turn status survives
settings success and recovers only through a later actual successful turn.
Fresh browser/native integration passed again against the rebuilt server and
source-matched assets. That checkpoint's unchanged frontend sources retained the earlier verified
**94 extension and 17 browser tests**, without rerunning those suites for the
CLI change. [Local CLI evidence](evidence/native-provider-profile-cli-local-provenance.json).
Exact revision `ac69c1f2a2c4a757b23d0d9da8b98d6188fab3a8` subsequently passed
its [hosted gate](https://github.com/xlang-foundation/xMind/actions/runs/37746664258):
**65 native contracts in 149.37 seconds**, **94 extension and 17 browser tests**,
native/browser integration and VSIX verification, with zero failures/skips and
the exact expected manifest.
[Hosted CLI evidence](evidence/native-provider-profile-cli-hosted-provenance.json).
That hosted result excludes the newer Claude agent/metrics and ordered-receipt
milestones. Installed
previews are unchanged; live Gemini inference, actual Gemini IDE acceptance and
full provider/coding/protocol parity remain incomplete.
[Provider setup](provider-setup.md), [CLI scope](native-interactive-cli.md).

The preceding `c4ec09fc` installed browser preview matched all 29/12 bundle/view hashes. Prior records and
settings were preserved; an actual read-only OpenAI Agent request, visible
metrics and subsequent refresh passed. Its original browser session needed one
reconnect; the exact cause remains unresolved. The disposable actual upgrade
and explicit rollback passed separately before installation.
[Historical installed acceptance](preview-checkpoint-c4ec09fc.md).
The preceding e353 backend upgrade, separate browser repair and first failed live
delegation request are recorded in [current acceptance](native-delegation-acceptance.md)
and [current validation](VALIDATION_STATUS.md).

All counts describe bounded verification, not product or OpenCode parity
percentages. Broad native providers, full coding workflows, remaining MCP/A2A,
graph behavior and Local/Nexus connection contracts remain active work.

## Historical native milestone records

The records below retain their original revision/time and fixture scope. Words
such as "current", "now", "pending" and installed-preview descriptions inside
these records apply to that historical checkpoint; the summary above is the
current source/verification status. No removed prototype is a supported runtime
or delivery requirement.

Native Responses provider checkpoint: explicit C++ wire selection now reaches the shared single-agent and graph engine, preserving stateless reasoning/tool continuation, actual supplied token metrics and persisted conversation replay. Two dependent agents perform actual file reads; provider-native items stay in child conversations rather than graph dependency/join outputs. Final local Release validation passed **52 native and 60 extension contracts**, no skips. [Scope and evidence](native-responses-provider.md). Inference/opaque reasoning are synthetic; live provider/IDE acceptance, interactive wire enrollment, standalone browser UI, broader provider coverage and full product parity remain pending.

The earlier native A2A v1 source `d07c61c725c3a1a4bf9ac85a2c6782575e52d8df` passed hosted **50 native and 60 extension contracts**, no skips, in [run 37693026697](https://github.com/xlang-foundation/xMind/actions/runs/37693026697). [Original full hosted job](evidence/native-a2a-v1-hosted-job.log). This result applies to that source, not the newer history/Responses changes or installed UI runtimes.

Native A2A history fidelity: protocol histories now preserve the admitted text-part boundaries, empty parts, and message/part metadata across both wire versions, completed stream replay and restart, while keeping model prompts separate and task histories isolated. Final local verification passed **50 native and 60 extension contracts**. The older flattening assertion's failure and its replacement checks are documented in [scope and evidence](native-a2a-v1.md). Hosted validation and preview installation of this source remain pending; full protocol/product parity remains incomplete.

Native A2A 1.0 core RPC checkpoint: direct versioned discovery/send/get/list/cancel/stream/subscribe now use the shared C++ executor and xlang3 repository, with default blocking semantics, cross-version durable retries and measured status times/cursor listing. The official SDK **1.3.0** exercised the native v1 wire without compatibility translation. Final local validation passed **50 native and 60 extension contracts**. [Scope, migration behavior and evidence](native-a2a-v1.md). Full protocol/product parity remains incomplete; hosted verification of this source and preview installation are pending.

Earlier native A2A discovery/streaming: authenticated discovery, Task-first SSE, persisted partial text/refusal, reconnect without reexecution and bounded viewer capacity now use the shared C++ executor and xlang3 journal. An official SDK **1.3.0** peer passed explicit v0.3 compatibility discovery/stream/get checks. The final isolated Release build passed **48 native and 60 extension contracts**. The hosted graph pause/shutdown failure is preserved and its ownership race is fixed with repeated regression cases. [Scope and evidence](native-a2a-streaming.md). Hosted verification at `b13ea224b026b9b750f43bdd311254ca7855b53c` passed all **48 native and 60 extension contracts** in [run 37689748037](https://github.com/xlang-foundation/xMind/actions/runs/37689748037). The newer checkpoint above adds native v1 and blocking; continuation, remote delegation and full platform parity remain incomplete. Previews retain their current runtimes.

Earlier native A2A message admission: nonblocking text requests now use real C++ agent admission and xlang3 persistence, with atomic durable retry identities and task-local histories. The isolated Release build passed **47 native and 60 extension contracts**, including rollback/migration and actual server/worker transport with explicitly synthetic provider replies. [Scope and evidence](native-a2a-message-admission.md). At that checkpoint discovery and streaming were incomplete; the newer milestone above adds these components. Remote delegation and full platform parity remain incomplete.

Earlier native A2A task-control component: the C++ `/a2a` adapter reads actual shared tasks/artifacts and requests owner-controlled cancellation, preserving accurate observed states and rejecting internal child IDs. Final local Release validation passed **45 native and 60 extension contracts**, including actual graph/file/SQLite task execution and repeated finish/cancel scheduling. [Scope and evidence](native-a2a-task-control.md). At that checkpoint, admission and task-local history were incomplete. The newer milestone above adds those components; the subsequent streaming checkpoint adds discovery and scoped SDK interoperability. Remote/full protocol interoperability remains incomplete. This is not full A2A compliance or an installed-preview claim.

Current graph UI checkpoint: the actual right sidebar submitted `read.repository.file`, paused for human input, read the real repository `README.md`, and displayed matching bytes. Its completed root and transcript survived an editor reload. The isolated `graph-ui` backend/profile preserves the user's existing model preview and open Settings dialog. Join output is compact and expandable, and workflow choice persists per backend. **60 local extension contracts** pass. [Actual IDE scope and evidence](vscode-graph-workflows.md). Agent graphs with live inference/metrics and the remaining full product scope are still incomplete.

The native graph ownership/HTTP/CLI checkpoint passed 44 native contracts locally, followed by seven final access regressions; the sidebar's production client passed against compiled C++/xlang3 persistence. [Backend commands and scope](native-graph-service.md). The newer hosted builds must be reported from their exact GitHub run outcomes; these local checks do not establish hosted success. Earlier milestone paragraphs below retain their original source/time scope.

Automatic account model discovery: installed backend `5815141dcf27652a1af503e39f17127ba189c403` passed **43 native and 47 extension contracts** in [isolated CI](https://github.com/xlang-foundation/xMind/actions/runs/37665810113). [CTest](evidence/native-model-discovery-saved-key-hosted-ctest.log), [original job/TAP](evidence/native-model-discovery-saved-key-passing-ci-job.log), [provenance](evidence/native-model-discovery-saved-key-hosted-provenance.json). Manual ID input is replaced by actual API discovery and a searchable picker. Encrypted saved keys can be reused without another password, and legacy credential-as-model records no longer advertise a key or enable inference. The native preview was upgraded with its database/profile/draft preserved. Its actual account discovery was rejected by OpenAI for authentication/authorization; the actual private key replacement prompt is open. The adapter-only replacement action passed **48 local extension contracts**, separately from the hosted backend checkpoint. [Launch/local adapter verification](evidence/vscode-model-discovery-launch.json), [scope](provider-setup.md). Successful account listing and live provider inference remain pending.

Provider setup installed: source `86ff065cafeb0b43f9bc47adb2d16b631dacade3` passed **42 native and 44 extension contracts** in [isolated CI](https://github.com/xlang-foundation/xMind/actions/runs/37657184396). [Hosted CTest](evidence/native-provider-setup-hosted-ctest.log), [original job/TAP](evidence/native-provider-setup-passing-ci-job.log), [provenance](evidence/native-provider-setup-hosted-provenance.json). The actual preview now runs this bundle and opened its native model setup prompt, preserving the database/profile and user's draft. [Launch evidence](evidence/vscode-provider-setup-launch.json), [scope](provider-setup.md). The native contract's provider replies/key are synthetic; entering the user's key and receiving an actual provider response remain pending. Subsequent CLI enrollment source is still under verification.

Graph executor source `77ffa610661abdab910b270e086c1bedf9f2a325` now passed **41 native and 41 extension contracts** in [isolated CI](https://github.com/xlang-foundation/xMind/actions/runs/37655834894). [Hosted CTest](evidence/native-graph-runner-hosted-ctest.log), [original job/TAP](evidence/native-graph-runner-passing-ci-job.log), [provenance](evidence/native-graph-runner-hosted-provenance.json). This proves the native component contracts with synthetic inference; public graph service/client workflows remain pending. The subsequent provider-setup source is still under exact-revision native verification, and the installed preview remains model-free.

Provider setup source checkpoint: native authenticated model/key enrollment, encrypted xlang3-backed credential references, idle-service replacement and persisted reopen are prepared with a bottom-composer setup control. All **44 extension contracts pass locally**, including private password/dismissal/late-grant fixtures. [Adapter output](evidence/vscode-provider-setup-local.log), [implementation and verification limits](provider-setup.md). Native compilation/integration remain pending behind the observed local xlang3 benchmark guard and isolated CI. The current preview has not been upgraded; no live chat or provider acceptance is claimed.

Native graph executor component: schema-v7 immutable task input, bounded parallel agent/tool dispatch, typed dependencies, conditional skips and durable human pause/resume now use actual backend workers. Release compilation and all **41 native contracts passed locally**. The graph contract exercises real file reads/creation, separate approvals, cancellation and injected outcome-journal failure/recovery without replay; provider replies remain synthetic. [Complete local build/CTest output](evidence/native-graph-runner-local-build-ctest.log), [source/binary provenance](evidence/native-graph-runner-local-provenance.json), [component and remaining scope](native-graph-runner.md). Public graph service/HTTP/CLI/view workflows and live-provider acceptance remain pending. The local sidebar launcher now accepts explicit model/endpoint/workspace settings and passes the provider key only to the native encrypted credential store; sidebar provider setup and a live response remain unverified while the user's key location is pending. All **41 extension contracts pass locally**: [output](evidence/vscode-model-startup-local.log).

Earlier durable checkpoint source `b9e96fceb8a8cc25db597f51b2a50583294b6009` passed **40 native and 41 extension contracts** in [isolated CI](https://github.com/xlang-foundation/xMind/actions/runs/37653997518). [Hosted CTest](evidence/native-graph-checkpoint-hosted-ctest.log), [original job/TAP](evidence/native-graph-checkpoint-passing-ci-job.log), [provenance](evidence/native-graph-checkpoint-hosted-provenance.json). The installed model-free preview remains on its earlier verified foreground-process bundle.

Durable graph coordinator checkpoint: schema-v6 admission, observed-child settlement, human wait/input and conditional skips commit their checkpoint/event updates together. All **40 native contracts passed locally**, including actual SQLite revision conflicts, event-fault rollback and pause/reopen/resume. [Complete output](evidence/native-graph-checkpoint-local-build-ctest.log), [provenance](evidence/native-graph-checkpoint-local-provenance.json), [scope](native-graph-checkpoints.md). Expanded hosted CI and public graph scheduling/client acceptance remain pending.

Earlier child-execution source `fbf2cfe9962c6d364893090e4590f765f92b61a3` passed **39 native and 41 extension contracts** in [isolated CI](https://github.com/xlang-foundation/xMind/actions/runs/37651959372). [Hosted CTest](evidence/native-graph-children-hosted-ctest.log), [original job/TAP](evidence/native-graph-children-passing-ci-job.log), [provenance](evidence/native-graph-children-hosted-provenance.json). Inference is synthetic; installed preview remains on its earlier verified bundle.

Native graph child checkpoint: schema-v5 parent/child records and private conversations pass all **39 native contracts locally**, including concurrent shared-engine child requests, actual file reads, atomic admission rollback, root retirement guards, identified event replay and reopen/recovery. Inference is synthetic; public graph scheduling and client workflows remain pending. [Complete output](evidence/native-graph-children-local-build-ctest.log), [provenance](evidence/native-graph-children-local-provenance.json), [boundaries](native-graph-children.md). Expanded hosted CI is pending.

Native graph foundation checkpoint: Release compilation and all **38 native contracts passed locally**, including dependency joins, typed conditions/references, human-wait checkpoint restore, interrupted-node uncertainty/non-replay, actual xlang3/SQLite catalog revisions/faults/reopen and authenticated native admin/server ownership. Coordinator outputs are synthetic; actual agent/tool graph execution and client workflows remain pending. [Complete output](evidence/native-graph-foundation-local-build-ctest.log), [provenance](evidence/native-graph-foundation-local-provenance.json), [component and integration contract](native-graph-foundation.md). Expanded hosted CI is pending.

Native repository guidance approval checkpoint: Release compilation and all **37 native and 41 extension contracts passed locally**. Durable edit/create/command proposals bind observed guidance sources; after approval, C++ rechecks before dispatch and retires mismatches without an effect. Actual changed/added/removed source contracts require a separate second approval, while sidebar contracts show recorded source hashes and reject malformed review data. [Native output](evidence/native-guidance-approval-local-build-ctest.log), [extension output](evidence/vscode-guidance-approval-local.log), [provenance](evidence/native-guidance-approval-local-provenance.json), [boundaries](native-repository-instructions.md). Expanded hosted CI and live/populated view acceptance remain pending.

Earlier repository discovery source `5122d6a1842ea92c0704fe297386c3a80622b881` passed **37 native and 40 extension contracts** in [isolated CI](https://github.com/xlang-foundation/xMind/actions/runs/37644164701). [Hosted CTest](evidence/native-repository-instructions-hosted-ctest.log), [original full job/TAP](evidence/native-repository-instructions-passing-ci-job.log), [hosted provenance](evidence/native-repository-instructions-hosted-provenance.json). Inference is synthetic; the installed backend preview remains on its earlier foreground-process bundle.

Native instruction configuration checkpoint: source `61a5d692d7d205df0d7f5a630f307c529e14dc15` passed **36 native and 40 extension contracts** in [isolated CI](https://github.com/xlang-foundation/xMind/actions/runs/37642796445). Actual xlang3/SQLite revisions/faults/reopen, admin/server/model/CLI delivery, immutable owner lease and continued native permissions are verified with synthetic inference. [Hosted CTest](evidence/native-agent-instructions-hosted-ctest.log), [original full job/TAP output](evidence/native-agent-instructions-passing-ci-job.log), [provenance](evidence/native-agent-instructions-hosted-provenance.json), [component limits](native-agent-instructions.md). The installed preview remains on the earlier foreground-process bundle.

Verified native foreground process checkpoint: source `3297a4bce2d591c0f1c1dd37ccc18a15297369e7` passed **35 native and 40 extension contracts** in [isolated CI](https://github.com/xlang-foundation/xMind/actions/runs/37635775031). Actual child/file effects cover descendant timeout/cancellation, executable binding, approved/denied/stale commands, bounded binary output, SQLite journal failures/reopen, retained-output cursor replay and native CLI watcher disconnect/reconnect. Inference is synthetic. [CTest](evidence/native-process-passing-ci-ctest.log), [full job/TAP log](evidence/native-process-passing-ci-job.log), [provenance](evidence/native-process-passing-ci-provenance.json). The actual [unseeded right-sidebar preview](evidence/vscode-native-process-bundle-sidebar.png) is running the tested bundle with its database/profile preserved. No live-model/populated coding claim follows; full shell/background/PTY, protocol/graph/provider/team/Electron scope remains required. Earlier source-prepared and failed records below remain historical.

Durable foreground output source checkpoint: the native executor journals a bounded byte-exact output prefix through embedded xlang3; the sidebar reconstructs independent stdout/stderr with explicit missing-sequence/binary handling. All **39 extension contracts passed locally** with labelled fixtures: [complete output](evidence/vscode-process-stream.log). Actual pre-exit output, SQLite replay/write failures and HTTP/CLI restart contracts are expanded in source and await exact-revision CI. The parent-exit cleanup correction remains included. There is no passing expanded process bundle or populated/live-provider editor acceptance yet. [Implementation and limits](native-process-tools.md).

The user requested visible milestones. Show a runnable result, validation evidence and remaining scope at each milestone. Prepared source or a diagram alone does not prove a runnable native milestone.

Next coding component: the native foreground process adapter, durable approval executor, executable binding, persisted profile administration and model/server/CLI source are prepared, with 35 native contracts expected by isolated CI. Local compilation was deferred by a currently observed xlang3 timing controller; no process pass or verified model/server/editor integration is claimed. [Implementation/acceptance contract](native-process-tools.md). The latest verified runnable product remains the 31-native/30-extension creation checkpoint below.

Sidebar command review/output contracts now pass all **34 local extension tests**, including literal argument review, escaped/control/hex output, actual supplied exit/timing/count fields, malformed/expired proposal rejection and uncertain-command handling: [complete output](evidence/vscode-process-review.log). These are labeled DOM/host fixtures, not a populated command run in the interactive preview. Native process profile configuration and end-to-end model/server/editor validation remain required.

## M1: persistence through embedded xlang3 — verified

Verified supporting component: native Windows credential protection compiled and passed its synthetic binary/context/tamper/ownership test. It is now connected to SQLite through embedded xlang3; see [credential-storage.md](credential-storage.md). The historical native test log `build/native/evidence/contracts-20261006.log` preserves the passing protection test and the two initial failing SQLite tests; the later passing logs below supersede those failures.

Release compilation and native seed/read demo passed on October 6, 2026 after the user-authorized SQLite module fixes on `agentflow/mcp-native-compat`. Seed persisted a session, two messages, agent configuration and four ordered events. Independent native read processes reopened that state; `-After 2` returned only events 3 and 4. All three default native contracts passed ten consecutive runs. Evidence: `build/native/evidence/contracts-after-sqlite-fix-20261006.log` and `contracts-repeat-after-sqlite-fix-20261006.log`. Earlier failure evidence remains in `contracts-20261006.log`.

The xlang3 legacy SQLite fixtures and the new isolation/text fixture also passed. This milestone proves initial native persistence contracts, not the agent engine, HTTP service, remote storage or UI. Encrypted SQLite credentials passed the following repository checkpoint. The other checkout's 97-case benchmark process exited before the module build; recheck processes before subsequent heavy builds.

C++ demo and contracts are prepared. The source creates a session, two conversation messages, a completed run, durable events and a general agent-configuration record, using only xlang3 for SQLite operations. A second process reopens the database and reads history/events; a cursor limits replay. Existing sessions are not overwritten by seeding. Encrypted secrets are not implemented by this demo.

On an idle machine:

```powershell
.\Tools\native-milestone.ps1 -Action Build
.\Tools\native-milestone.ps1 -Action Seed
.\Tools\native-milestone.ps1 -Action Read
.\Tools\native-milestone.ps1 -Action Read -After 2
```

The build action refuses to run while observed xlang3 benchmark processes are live. Exit 3 means deferred, not passed. Build/configuration and contract failures propagate nonzero exit codes. The launcher never executes CPython. Build output and the demo database are separate from the installed WorkSense application and other xlang3 checkout.

Delivered evidence: Release build, passing default embedded adapter/repository contracts, seed/read in separate native processes and exact cursor replay. Affected-row assertions remain intact. Wider Python engine/A2A historical failures and graph closure behavior are separate outstanding investigations; the native milestone does not establish their resolution.

## Following milestones

Verified native creation/SDK checkpoint: [isolated run 37621404384](https://github.com/xlang-foundation/xMind/actions/runs/37621404384), exact source `e93f28c7e6d7c214f1c0d5e049ad9408128b4ad5`, passed Release compilation and all **31 native and 30 extension contracts**. Actual files establish approved creation including empty/Unicode content, no overwrite, recorded parent identity, directory-junction rejection, denial/cancellation before dispatch, real result continuation, pending-approval restart and actual creation followed by an outcome-storage fault recovered as uncertain without replay. Official modern/legacy MCP SDK peers and both native schema dialects also passed. [Complete CTest output](evidence/native-file-creation-ci-ctest.log), [provenance](evidence/native-file-creation-ci-provenance.json), [scope and remaining creation work](native-file-creation.md). Inference is synthetic; populated editor/live-provider execution, cancellation after physical creation, process death during partial writes and attributed creation reconciliation remain unverified. The actual unseeded sidebar now runs this tested development bundle with its preserved session/profile and no configured model: [screenshot](evidence/vscode-native-creation-bundle-sidebar.png), [launch metadata](evidence/vscode-native-creation-bundle-launch.json). The next coding component is the [native process executor](native-process-tools.md); its acceptance contract is documented and implementation remains pending. Earlier pending/source-only statements below are superseded by this exact result. The full project goal remains active.

Native creation implementation prepared, **not yet natively verified**: [source scope](native-file-creation.md). The agent/executor and opened-handle create-new path now have contracts for actual files, conflicts, parent identity, junction rejection, Unicode/empty content and outcome-storage fault/restart; model/HTTP/CLI creation cases are prepared too. All **30 extension contracts passed locally** for absence-aware review/comparison and uncertainty rendering: [original extension output](evidence/vscode-native-creation-review.log). Local native compilation deferred during the separate xlang3 full timing suite. The prior thirty-contract SDK [CI run](https://github.com/xlang-foundation/xMind/actions/runs/37619215608) failed before native configuration because two `npm.cmd` paths were combined; [actual error excerpt](evidence/native-mcp-sdk-ci-resolver-failure.log). Corrected source `e93f28c7e6d7c214f1c0d5e049ad9408128b4ad5` selects one application and schedules the complete proposed 31-contract [isolated run 37621404384](https://github.com/xlang-foundation/xMind/actions/runs/37621404384). New native behavior is pending, and the goal remains active.

Official SDK tool interoperability checkpoint: source `bbeb4db077adcb5cbb86c9e91d3d3fe199886e0f` compiled in Release and passed all **30 native contracts locally**, including actual modern SDK 2.3.1 and legacy SDK 1.32.1 stdio peers. The native agent/server/admin/CLI path verifies discovery/protocol selection, encrypted environment, exact incoming argument bytes, approved real file effects, denial/cancellation, result continuation and persisted lost-reply uncertainty without replay. Native schema validation now retains distinct Draft-07 and 2020-12 semantics inside the existing bounded worker; tests exercise legacy tuple/dependency rules and `$ref` sibling differences. [Original full build/CTest output](evidence/native-mcp-sdk-local-build-ctest.log), [source/runtime/worker and pinned SDK provenance](evidence/native-mcp-sdk-local-provenance.json), [fixture scope](../Native/tests/sdk/README.md). Inference is synthetic. Full SDK feature coverage, HTTP/OAuth/resources/prompts, modern interactive continuations, account-resource mapping, complete schema conformance and live/populated UI execution remain incomplete. [Isolated CI run 37619215608](https://github.com/xlang-foundation/xMind/actions/runs/37619215608) is in progress; the goal remains active.

The preceding configured-MCP revision `1a3d5c1d5542cc1ce8eb3db1162063f7e5425b9c` passed isolated [CI run 37617251064](https://github.com/xlang-foundation/xMind/actions/runs/37617251064), all **28 native and 28 extension contracts**: [CTest output](evidence/native-agent-mcp-ci-ctest.log), [provenance](evidence/native-agent-mcp-ci-provenance.json). It predates Draft-07 validation and official SDK peer contracts.

Checkout/preview checkpoint: source, Git history, editor profile and session database now live in `D:\CantorAI2026\xMind`. The actual normal development host reopened its existing session with Explorer left and xMind/composer/model chooser right, using the locally verified MCP integration binary and no model or seeded messages: [launch evidence](evidence/vscode-xmind-checkout-launch.json), [actual screenshot](evidence/vscode-xmind-checkout-sidebar.png). A fresh CMake configure/Release build in `xMind\build\native` passed all **28 native contracts** after relocation: [original complete output](evidence/native-xmind-root-build-ctest.log), [source/binary provenance](evidence/native-xmind-root-build-provenance.json). Native and extension implementation trees are unchanged from `1a3d5c1`; intervening commits update documentation/evidence. Only prior locked build artifacts remain under the old `AgentFlow\build` directory; the active checkout and fresh build use xMind.

Native configured MCP agent checkpoint: Release compilation and all **twenty-eight** native contracts passed locally at `1a3d5c1d5542cc1ce8eb3db1162063f7e5425b9c`. The real compiled administrator persists registered stdio configuration and command-bound DPAPI credential references through embedded xlang3. The actual server restores those settings, discovers subprocess tools per run, exposes them to the agent and routes exact proposals through controller approval and durable multi-resource claims. Independent MCP peers actually write fixture files; synthetic inference verifies continuation after peer acknowledgement, denial/cancellation and lost-reply uncertainty retained across restart without replay. All **twenty-eight extension contracts** passed, including external-tool approval labels and uncertainty rendering. [Original build/CTest evidence](evidence/native-agent-mcp-local-build-ctest.log), [source/binary provenance](evidence/native-agent-mcp-local-provenance.json), [renderer/extension evidence](evidence/vscode-mcp-renderer.log), [trusted setup](native-mcp.md). Actual populated editor MCP execution, live inference, SDK interoperability, Streamable HTTP/OAuth, account mapping and complete schema conformance remain incomplete. [Isolated CI run 37617251064](https://github.com/xlang-foundation/xMind/actions/runs/37617251064) is in progress; the goal remains active.

Native MCP resource/worker checkpoint: Release compilation and all **twenty-six** native contracts passed locally at `9582dee1e17f37fe5e00220dda4c1a63f53188fe`. Repository schema four atomically locks the opened workspace and stable configured-server resources; real subprocess effects verify cross-workspace quarantine, while repository fixtures verify contention, revision changes and migration/backfill/rollback. Schema admission and input/output validation use a bounded native child with real Windows memory/time/process budgets. Independent fixtures verify actual memory-allocation refusal, stalled-child timeout/cancellation and subsequent healthy production-worker validation. Worker failures, genuine schema rejection and peer tool errors remain distinct; claimed cancellation/result-storage failures retain fail-stop ownership. [Complete build/CTest evidence](evidence/native-mcp-resources-schema-local-build-ctest.log), [source/binary provenance](evidence/native-mcp-resources-schema-local-provenance.json). Its isolated [CI run 37614949089](https://github.com/xlang-foundation/xMind/actions/runs/37614949089) also passed, all twenty-six native and twenty-seven extension contracts: [CTest evidence](evidence/native-mcp-resources-schema-ci-ctest.log), [provenance](evidence/native-mcp-resources-schema-ci-provenance.json). That revision predates registered agent activation above. SDK interoperability, account-resource mapping and full upstream schema conformance remain incomplete; exact OS CPU-timer scheduling is not proved.

The preceding approval-backed MCP source passed [isolated CI run 37610809269](https://github.com/xlang-foundation/xMind/actions/runs/37610809269) at `5b24f26a6ee2dd5e3da06aaee6169b5513a6a980`, all twenty-five native and twenty-seven extension contracts: [CTest evidence](evidence/native-mcp-effects-ci-ctest.log), [provenance](evidence/native-mcp-effects-ci-provenance.json). That revision predates shared-resource migration and schema-worker containment; it does not prove their isolated CI result.

Native approval-backed MCP checkpoint: Release compilation and all **twenty-five** native contracts passed locally at `5b24f26a6ee2dd5e3da06aaee6169b5513a6a980`. Actual independent MCP subprocesses wrote real fixture files only after exact durable approval. Tests cover denied/cancelled/pre-dispatch effects, duplicate identities, exact numeric/schema/config binding, invalid catalogs/arguments/output, lost/error replies after actual effects, workspace quarantine and outcome-storage fault/restart without replay. Native JSON Schema 2020-12 validation and all 176 pinned library/source license hashes passed too. [Complete build/CTest evidence](evidence/native-mcp-effects-local-build-ctest.log), [binary/source provenance](evidence/native-mcp-effects-local-provenance.json). Peer acknowledgement is explicitly distinguished from independent verification. Registered configuration persistence, agent/CLI/view wiring, SDK interoperability, full schema conformance, adversarial evaluation containment and server/account-wide resource locking remain incomplete. [Isolated CI run 37610809269](https://github.com/xlang-foundation/xMind/actions/runs/37610809269) is in progress; this is a local component checkpoint, not full MCP/product readiness.

The preceding native catalog client additionally passed [isolated CI run 37608803467](https://github.com/xlang-foundation/xMind/actions/runs/37608803467) at `fe07a95d5984748aaabbd744a76bda9d75bb45ef`, all twenty-three native and twenty-seven extension contracts: [CTest evidence](evidence/native-mcp-client-ci-ctest.log), [provenance](evidence/native-mcp-client-ci-provenance.json). That revision precedes schema/effect integration and does not prove the new twenty-five-contract source.

Isolated negotiation checkpoint: [CI run 37607766121](https://github.com/xlang-foundation/xMind/actions/runs/37607766121) passed all twenty-two native and twenty-seven extension contracts at `809ef148ba5a16778a0a1a98ca06637394497c00`. Actual independent peers verify modern discovery, legacy error/timeout fallback, retired late discovery replies, initialized notification ordering and each supported legacy revision (`2025-11-25`, `2025-06-18`, `2024-11-05`). [CTest evidence](evidence/native-mcp-handshake-ci-ctest.log), [provenance](evidence/native-mcp-handshake-ci-provenance.json). That revision precedes the peer-request reply helper and catalog client; it does not establish their isolated CI result or model/tool integration.

Native MCP client checkpoint: Release compilation and all twenty-three native contracts passed locally at `fe07a95d5984748aaabbd744a76bda9d75bb45ef`. The owned native subprocess client discovers real peer catalogs with pagination, validates bounded descriptions, handles legacy ping/unsupported methods and retires on malformed or interrupted exchanges without reconnect/replay. Modern/legacy negotiation and transport contracts also passed. [CTest evidence](evidence/native-mcp-client-local-ctest.log), [runtime binary provenance](evidence/native-mcp-client-local-provenance.json). Isolated CI is pending. Tool execution remains absent until policy/journaling integration; full schema validation, resources/prompts and SDK interoperability remain required.

Native MCP request-tracking checkpoint: [CI run 37602231619](https://github.com/xlang-foundation/xMind/actions/runs/37602231619) passed all twenty native and twenty-seven extension contracts at `276164689deacb130a66fce4362ad94d07cd12b7`. Actual compiled correlation fixtures verify bounded admission, out-of-order matching, peer errors, duplicates/unknown IDs, cancellation, initialization cancellation rejection, abandonment and retirement bounds. [CTest log](evidence/native-mcp-requests-ci-ctest.log), [provenance](evidence/native-mcp-requests-ci-provenance.json). Windows stdio/process fixtures subsequently passed [CI run 37603832587](https://github.com/xlang-foundation/xMind/actions/runs/37603832587), all twenty-one native and twenty-seven extension contracts at `a98cf0bc59d0088d9e64a36b43d064b5bcbaf3cc`, including waiting-reader cancellation. [CTest evidence](evidence/native-mcp-stdio-cancellation-ci-ctest.log), [provenance](evidence/native-mcp-stdio-cancellation-ci-provenance.json). Modern/legacy negotiation and peer-request replies subsequently passed locally with the twenty-three-contract client checkpoint above; isolated CI and tool/policy integration remain required.

Native MCP wire checkpoint: [CI run 37601682734](https://github.com/xlang-foundation/xMind/actions/runs/37601682734) passed Release compilation, all twenty native contracts and twenty-seven extension contracts at `f0e44b029d6a28a57a80f77e921641301105a104`. The new real C++ codec passed bounded fragmented JSON-RPC/UTF-8 fixtures, precise IDs, modern request metadata and rejection/EOF behavior. [CTest log](evidence/native-mcp-wire-ci-ctest.log), [provenance](evidence/native-mcp-wire-ci-provenance.json). Added request tracking and Windows owned-process/stdin/stdout transport are subsequent source awaiting their own native runs. This is not full MCP peer/tool integration, A2A or graph completion.

Verified uncertainty inspection checkpoint: [CI run 37600409106](https://github.com/xlang-foundation/xMind/actions/runs/37600409106), revision `cbc5f26c3c1842a54013f94cbf3baf303761b568`, passed all nineteen native and twenty-three extension contracts. The actual production server reopened the fixture journal without a model, inspected bounded raw file bytes through HTTP/CLI/editor host client, rejected another workspace and preserved the uncertain operation/events. [CTest evidence](evidence/native-uncertain-inspection-ci-ctest.log), [provenance](evidence/native-uncertain-inspection-ci-provenance.json). Its packaged runtime now runs in the [actual unseeded sidebar](evidence/vscode-inspection-bundle-launch.json); no inspection workspace or provider is configured in this preview. Attributed resolution and actual populated recovery UI remain incomplete. The new MCP codec is separate source awaiting the twenty-contract native gate.

Recorded-run history checkpoint: the sidebar now exposes each backend run in the selected conversation, preserving full transcript history while choosing older events/operations. Selected-run identity persists by origin/session, and IDs from another conversation are rejected before fetching their details. Older uncertain edits remain inspectable. Selecting historical work preserves observation of another active run and blocks additional submissions. [Twenty-seven extension contracts](evidence/vscode-run-history.log) passed against labeled HTTP/VS Code API/DOM fixtures. The [actual unseeded preview reopened](evidence/vscode-run-history-preview.json) with saved SecretStorage against the existing verified native bundle; [screenshot](evidence/vscode-run-history-preview.png). Actual populated history rendering and live model execution remain separate verification requirements.

Uncertain-edit inspection source checkpoint: native read-only HTTP/CLI access and an explicit model-free inspection workspace now connect the existing raw-file inspector to clients. The VS Code sidebar offers **Inspect actual file**, displays the backend observation and preserves quarantine without any grant/replay/restore action. [Twenty-three extension contracts](evidence/vscode-uncertain-inspection.log) passed locally, including escaped observations and selection invalidation. Native production restart/identity/journal contracts are added and await hosted CI; this entry does not claim their pass or actual uncertain-edit rendering in the interactive preview.

Verified development bundle checkpoint: [CI run 37598517059](https://github.com/xlang-foundation/xMind/actions/runs/37598517059) passed all nineteen native contracts, including selected-model CLI initiation of an actual approved file edit, and twenty extension contracts. Its packaged C++ server and embedded xlang3 runtime now run in the actual local VS Code preview with the existing session database, right sidebar and bottom model selector. [CTest evidence](evidence/native-model-edit-cli-ci-ctest.log), [launch evidence](evidence/vscode-ci-bundle-launch.json), [actual screenshot](evidence/vscode-ci-bundle-sidebar.png). Inference contract peers are synthetic; the preview remains unconfigured for live inference. The development bundle requires external pure standard-library source and is not a complete installer.

Native model/edit integration checkpoint: Release compilation and all **nineteen native contracts** passed on an isolated Windows runner, with stock pinned xlang3 plus the reviewed SQLite prerequisite and allowed standard-library source. Actual model-invoked file application, denial/stale/cancel handling, model selection, supplied usage/measured timings, restart retention and uncertain-file inspection passed using labeled synthetic inference peers. [CI run](https://github.com/xlang-foundation/xMind/actions/runs/37595553681), [complete CTest evidence](evidence/native-model-edit-ci-ctest.log), [provenance](evidence/native-model-edit-ci-provenance.json). All twenty extension contracts also passed. Live-provider/coding completion, new-file/process tools, attributed reconciliation, MCP/A2A, graphs and team/Electron scope remain incomplete. The local preview has no model; its subsequent bundle upgrade is recorded above.

Persistent interactive preview checkpoint: the launcher now uses a normal VS Code extension development host. The old `--extensionTestsPath` bootstrap selected in-memory VS Code storage, losing its server access token on restart; that test mode is retained only for actual tests. Normal-host development bootstrap clears private environment variables, completes activation before opening the view, and records a unique readiness marker. Actual close/reopen reused the saved SecretStorage token against the same native backend without supplying another token. [Reopen evidence](evidence/vscode-preview-reopen.json), [actual screenshot](evidence/vscode-persistent-sidebar.png), and [twenty passing extension contracts](evidence/vscode-preview-persistence.log). No model is configured. Actual read-only diff integration also passed again after activation changes.

Actual VS Code diff checkpoint: `Tools/test-vscode-review.ps1` launches an isolated official VS Code host and runs the product snapshot/diff adapter against labeled test bytes. VS Code 1.140.0 opened a real native diff tab with exact before/after documents; typing and filesystem writes could not alter the read-only snapshot, which stayed clean. [Actual-host evidence](evidence/vscode-native-diff.json). This is editor API integration evidence, not a rendered screenshot or live model/approval/file-effect test. The interactive preview is not seeded with these fixtures.

VS Code reconnect checkpoint: Refresh now rechecks backend health/model capabilities, reloads the selected conversation and resumes observation without creating or replaying an agent run. Configured model selection persists by backend origin across view reopening; removed IDs fall back to the backend default. The composer draft stays in the view. Nineteen deterministic extension contracts pass: [reconnect evidence](evidence/vscode-reconnect.log). This verifies adapter behavior with HTTP/host fixtures; actual backend model reconfiguration/live provider execution remains unverified.

VS Code edit comparison checkpoint: pending file approvals now offer **Compare changes**, opening the recorded before/after snapshots in VS Code's native read-only diff editor. The host fetches and revalidates the reviewed operation before opening; webview-supplied text is ignored, and comparison sends no approval or filesystem write. Snapshot storage is bounded to 32 comparisons/32 MiB per extension activation. Seventeen client/host/renderer fixture contracts pass, including exact snapshot comparison and changed-proposal rejection: [comparison evidence](evidence/vscode-edit-comparison.log). Actual live model-driven proposal/diff interaction remains unverified while the native integration is awaiting compilation.

Right-sidebar chat checkpoint: the real VS Code 1.140 development host renders xMind in the right secondary sidebar, with Explorer left and the composer/model selector anchored below scrolling history. Fifteen client/host/DOM tests cover sanitized Markdown/code, per-response usage badges, missing metrics, approvals, backend-advertised model choices and live-prefix preservation. [Actual unseeded UI screenshot](evidence/vscode-right-sidebar.png) and [test evidence](evidence/vscode-sidebar-renderer.log) verify that scope. No model is configured in the preview. Native per-run model selection, persisted timings/usage and model-invoked edits are pending verification behind the live benchmark build guard; live history/inference and full coding completion remain unverified.

VS Code approval view checkpoint: the view displays recorded operation payloads and before/after text, and sends allow/deny choices through the authenticated host adapter. The host requires a reviewed pending proposal in the selected run and revalidates the exact backend record before granting. Eight deterministic client/host contracts pass, including changed/unreviewed/repeated decision rejection and disposal during revalidation: [approval host evidence](evidence/vscode-approval-host.log). Actual IDE rendering, model-invoked edits, reconciliation and team permissions remain unverified or pending.

Native local approval API checkpoint: authenticated local-owner inspection/decision routes, CLI `operations`/`operation`/`decide`, and the thin VS Code host client now connect to durable exact proposals. The actual compiled server and executor apply approved real-file edits and reject stale/denied effects. [Approval API evidence](evidence/native-approval-api-ctest.log) covers eighteen contracts, including unauthorized/spoofed decisions and absent client effect-mutation routes. Agent write-tool integration, approval UI, reconciliation and team scopes remain pending.

Native approved edit component: `EditExecutor` now connects exact durable before/after proposals, attributed approval and one-use claims to actual file application and outcome journaling. Real file/xlang3 tests cover approval, denial, cancellation, stale plans, duplicates and reopen. A fixture-only outcome-storage fault after the actual edit verifies that the file effect remains observable and restart quarantines the unresolved claim. [Approved edit evidence](evidence/native-approved-edit-ctest.log) covers seventeen native contracts. Public controller authorization/routes, agent write tools, partial-write/process-kill testing and reconciliation remain pending.

Native file application component: the backend-only Windows primitive revalidates the exact file snapshot, applies real in-place changes and checks the resulting bytes. Real disk contracts cover longer/shorter/empty edits, stale content, identity/hash/boundary rejection, pre-write cancellation and competing handles. [Application evidence](evidence/native-file-application-ctest.log) covers the sixteen native contracts. Integrated approval/execution, post-write fault recovery and reconciliation remain pending; model tool definitions still offer only read operations.

Native authorization/planning checkpoint: schema-v3 operation records and `PermissionWaiter` now preserve exact approvals, controller attribution, single-use claims, cancellation/expiry, workspace exclusion and uncertain recovery. Workspace identity, same-handle content snapshots and literal edit planning passed against real filesystem fixtures and independent Node hashes. All sixteen native contracts passed: [native-permission-planning-ctest.log](evidence/native-permission-planning-ctest.log). These are library components; public approval controllers, integrated approved file execution and reconciliation remain required before exposing a write tool.

Native execution/server checkpoint: [AgentRunner and AgentService](native-agent-loop.md) invoke the native provider and actual read tools, with encrypted credential resolution, atomic conversation/state persistence, bounded native workers, HTTP/CLI scheduling, cancellation and run deadlines. All fifteen native contracts passed, including actual editor host client requests to the compiled server. Evidence: [native-execution-server-ctest.log](evidence/native-execution-server-ctest.log). Live inference, coding-task completion and actual editor UI validation remain pending.

Native workspace tools checkpoint: [actual C++ filesystem tools](native-workspace-tools.md) read, list and search authorized Windows workspace files, with handle checks, argument validation and bounded results. Real filesystem fixtures and native agent integration passed. Mutation/process tools, permission policy and complete coding workflows remain required.

Native HTTP/console checkpoint: [xMind Server](native-server.md) exposes session persistence and configured native execution, with authenticated independent HTTP/CLI/editor-host clients. Runs and cancellation are owned by the real engine; manual lifecycle transitions and fabricated client assistant messages are rejected. Read-tool execution, cancellation and durable event observation passed with synthetic inference peers. Live provider and full coding workflows remain required.

Native persistence worker checkpoint: [PersistenceService](persistence-service.md) owns embedded xlang3 and the repository on one thread, serving typed requests from concurrent backend callers. All five native contracts passed, including concurrent writes, queue backpressure, owner recovery, failure cleanup and shutdown draining. It is ready for native HTTP/agent callers; those services and their authentication remain incomplete.

Credential storage checkpoint: the C++ repository now protects and persists credentials through xlang3, with scope/purpose binding, revision-checked rotation/deletion and retired identities. Atomic schema-v1 migration preserves existing messages. All four native contracts passed; the original milestone database also reopened and replayed events after migration. Committed evidence: [credential-repository-ctest.log](evidence/credential-repository-ctest.log). No team authorization or public credential endpoint is claimed.

- Native xMind Server + CLI: shared sessions/run events, lifecycle, authentication and reconnection.
- Coding engine: real model/tool task with explicit approvals, verification and reviewable changes.
- Protocols and graphs: independent MCP/A2A peers, graph branching/pauses/checkpoint recovery.
- Clients: browser UI and VS Code workflows over the local backend, with actual UI validation.
- OSS release: reproducible native builds, scoped parity evidence, documented limitations and end-to-end local coding/protocol/provider acceptance.
- Connection profiles: Local single-user mode plus optional Nexus binding over the shared protocol, scoped identities/state, authenticated local agent/workspace enrollment and real reconnect/lease/recovery acceptance. Nexus owns the private team-server/distributed implementation.

These milestones retain the revised xMind OSS scope. Team-server features, PostgreSQL, WebRTC and Electron belong to Nexus and are excluded. OpenCode is a workflow reference only; xMind implements the coding capabilities selected for its own product. Agreed broad model/provider support remains required.

2026-10-14 live Gemini 404 UI diagnosis: the browser run against `models/gemini-2.5-flash` returned HTTP 404 without a recognized structured provider code. The saved failure now renders a safe, actionable model/endpoint message for every provider HTTP 404, while preserving allowlisted code details when present and never showing provider response bodies. The fresh-profile TestProj browser acceptance and 89/89 shared renderer tests passed. [Evidence](evidence/gemini-404-diagnostic-live-20261014.json). The previously uncertain production-preview operation remains untouched.

2026-10-11 native console terminal checkpoint: commit `7e3c896` adds Windows console key-to-VT forwarding, Unicode/control/navigation/function-key mapping, terminal sizing from the attached console, and live resize observation while preserving the scripted line-input path. This follow-up adds a bounded/coalescing keystroke queue and a separate resize transport. The actual-server CTest launches unified `xmind` inside a nested Windows ConPTY, types shell commands while the console resizes to 110×42, then verifies shell-reported dimensions, output and cleanup; the contract passed twice consecutively. The mapping contract also passes. Full-screen application, mouse, VS Code live acceptance and complete OpenCode CLI parity remain open.

2026-10-11 workspace checkpoint: a workspace can start from one selected folder, then attach more folders while keeping its identity and primary folder. The unified CLI provides `workspace init`, `workspace info` and `workspace add-folder`; the browser exposes **New workspace** and **Add folder** on the same local backend. New graph runs save the exact workspace membership with root admission; graph read nodes and graph-agent children use that immutable folder set. Single-agent `edit_file` and `create_file` now select a registered folder, and the write approval binds workspace revision, folder identity and root identity. Commits `b2c4087` and `499dba2` are pushed to `checkpoint/native-persistence-m1`. The Release build and focused workspace-tools, agent-runner, graph-runner, create-executor and edit-executor contracts pass (5/5); the attached edit fixture changed only the selected secondary folder. A single `apply_patch` batch cannot span roots; Git and language-server routing remain primary-folder-only. Client contracts and full live multi-folder acceptance remain open.

2026-10-11 multi-folder coding effects checkpoint: AgentRunner and GraphRunner now route process, create and edit tools to explicitly selected members in the immutable workspace snapshot. The agent model/tool loop and graph nodes launched real PowerShell and verified the created file appeared only under the selected secondary folder. Graph create/edit nodes used controller-approved proposals, checked the exact workspace/folder/revision in each journal entry, and applied the exact reviewed content only in that folder. Trusted process operations retained `trusted-process-profile:powershell` attribution. `native_agent_runner_contract` and `native_graph_runner_contract` both pass locally using synthetic model peers and real native effects; this does not establish live-model UI acceptance. Browser TestProj acceptance separately ran its actual Vite build through xMind, exit code 0. CLI/VS Code multi-folder effect acceptance, cross-root `apply_patch`, multi-root Git/language servers and complete OpenCode coding/CLI parity remain open.

2026-10-11 multi-folder patch checkpoint: AgentRunner and GraphRunner now route `apply_patch` to an explicitly selected folder in the admitted workspace snapshot. Each patch file remains a separate controller-approved operation; its immutable proposal binds the stable workspace ID, folder ID, workspace revision and selected native root. Real filesystem contracts verify a model-issued agent patch and a registered graph patch create files only in the selected secondary root, and that the model receives the durable patch receipt after approval. The six focused agent/graph/patch contracts pass locally; the Release-with-debug-info unified `xmind` executable builds. Provider responses are synthetic fixtures, while patch planning, approvals, xlang3-backed journals and filesystem effects are real. A single patch batch still cannot span roots; CLI/VS Code multi-folder acceptance, multi-root Git/language servers and full OpenCode parity remain open.

2026-10-13 browser transcript checkpoint: the local browser adapter validates bounded session-history page sizes and cursors, and the native View-session route allowlist now permits authenticated read-only transcript pages. A real compiled xMind/xlang3 backend contract confirms the browser can reload saved session messages through its HttpOnly cookie without replaying the completed run. All 107 browser tests and the native browser contract pass; the native `xmind` Release target builds. This validates the adapter/backend contract, not live user acceptance in an external browser. xlang3 changes remain limited to runtime gaps required by xMind.

2026-10-13 single-message retrieval checkpoint: the shared client now exposes validated `sessionMessage(session, message_id)` reads, and the browser adapter permits only GET for one exact session/message route with no query string. Browser tests reject query-bearing and write requests before forwarding. The compiled xMind/xlang3 integration contract retrieves a saved transcript entry through the HttpOnly browser session after reconnect. The VS Code suite passes 334/334 and the browser suite 107/107. OpenCode's richer message/part schema remains a separate parity gap; no xlang3 source change was needed.

2026-10-11 A2A artifacts checkpoint: outbound task inspection accepts an explicit `includeArtifacts` request and retains the returned task/artifact payload in the bounded xlang3 SQLite observation. CLI users can pass `a2a-inspect OPERATION_ID --include-artifacts`; browser and VS Code expose a separate **Fetch remote artifacts** control. The normal refresh does not request artifacts. Rendered peer content remains untrusted JSON: xMind does not follow peer URLs or write artifacts into workspace folders. Local synthetic contracts exercise request propagation; official SDK/independent-peer conformance and installed/rendered acceptance remain open.

2026-10-11 default Windows process profile checkpoint: a new Windows backend now initializes a built-in PowerShell profile, falling back to the system `cmd.exe`, only when no process registry exists. The built-in local shell profile auto-approves commands to keep the coding workflow uninterrupted. Saved custom profiles and an explicitly empty `profiles: []` registry are not overwritten or reseeded. The Release `xmind` and process-configuration contract targets build; the process configuration/executor/agent HTTP contracts pass **3/3**, and a fresh managed local-view profile exposes only the sanitized process catalogue (**1/1**). Shell semantics, model-driven rendered browser/VS Code command acceptance and full OpenCode coding parity remain open.
2026-10-12 Claude stream diagnostics checkpoint: Claude SSE `overloaded_error`, `rate_limit_error` and `api_error` now produce stable safe xMind diagnostic codes; unknown SSE errors remain generic, and provider message text/request identifiers are never retained. The VS Code run panel maps those codes to actionable messages. A synthetic native decoder contract passes after a Release rebuild, and all 318 VS Code extension contracts pass. This only improves diagnosis for the next live run: the prior TestProj coding runs remain incomplete and the current VS Code preview has not been upgraded/retested against this diagnostic build.

2026-10-12 Local/Nexus catalog checkpoint: a native C++ `ConnectionProfiles` store now persists the profile catalog through embedded xlang3 SQLite. First initialization writes an active Local profile; YAML/JSON updates use catalog and per-profile revisions, reject credentials/unknown fields and non-HTTPS Nexus origins, and permanently retire removed IDs. The isolated Release build used `D:\CantorAI2026\xlang3\build\Release`, then passed the complete **136/136 native CTest contracts** in 621 seconds, including existing xlang3 SQLite/persistence/local-profile checks and the new connection-profile contract ([full output](evidence/native-connections-main-ctest-20261012.log)). A follow-up tightened host and port validation and reran the focused contract successfully ([focused output](evidence/native-connection-profiles-focused-20261012.log)). This is catalog persistence only: `ConnectionProfiles` is not yet connected to HTTP/CLI or browser/VS Code selection, and Nexus authentication, encrypted connection credentials, remote request routing and workspace enrollment remain incomplete. The older runtime checkout at `D:\CantorAI\xlang3` lacks the SQLite `isolation_level` keyword and must not be used for xMind builds.

2026-10-12 browser connection-settings checkpoint: the browser Settings dialog now reads the native Local/Nexus catalog and can save/remove public Nexus address metadata through the exact allowlisted `GET`/`POST /v1/connections` route; server credentials, provider keys and profile selection are not exposed. The adapter controller, classic-script renderer, client validation and right VS Code Settings share this backend contract. The complete browser/VS Code UI suite passes **419/419** (`node --test extensions/vscode/tests/*.test.js views/browser/tests/*.test.cjs`; [full output](evidence/browser-vscode-connection-settings-20261012.log)). The real external browser did not confirm a live catalog read: updating its adapter invalidated the old in-memory local launch credential, and the replacement view reported authentication required. No live profile save/removal or remote connection behavior is claimed. Nexus authentication, profile selection/routing and workspace enrollment remain open.

2026-10-12 generic model-response diagnostics checkpoint: unclassified `ModelProtocolError` failures now persist the fixed code `model_response_invalid`; provider exception text is never copied into the diagnostic. The VS Code run panel accepts only this exact code and continues to suppress arbitrary/credential-shaped strings. Release rebuild of the shared model library, `model_stream_contract` and `responses_contract` succeeded; focused CTest passes **2/2**, and the complete browser/VS Code suite remains **419/419**. Evidence: [native diagnostics CTest](evidence/model-response-safe-diagnostic-ctest-20261012.log) and [full UI tests](evidence/browser-vscode-connection-settings-20261012.log). This improves observability for the two earlier failed Claude coding runs; it does not identify their underlying validation mismatch, complete those runs or claim live-model acceptance.

2026-10-12 TestProj browser coding checkpoint: using the existing local xMind profile and TestProj workspace, Claude read the Model Zoo README and six frontend files, then applied the approved scoped catalog update. A separate xMind native process run built `model-zoo/frontend` (Vite 6.4.4, 36 modules, exit 0, 640 ms, no stderr). In a follow-up conversation, Claude restored keyboard-accessible removable active-filter buttons in `HomeView.vue`; the one-file edit was followed by another actual xMind `run_process` build (exit 0, 583 ms, no stderr). Both original model-assisted coding conversations ended with provider HTTP 400 (`invalid_request_error`) before a final assistant response. A subsequent no-tools turn successfully summarized the persisted edit/build results (Claude Sonnet 5.5, 21,695 input and 590 output tokens, first token 5.13 s, total 5.26 s); this proves recovery on a new turn, not automatic recovery or the reason for the original 400. The live Vue/FastAPI page returned HTTP 200 for the frontend, API docs, OpenAPI, health, catalog stats and model-list routes. Browser checks narrowed 12 models to 3 via task filtering, restored 12 via Remove filter, returned 2 matches for `bert` before Reset, and opened GPT-2 details. The earlier DeepSeek conversation reached about 64,000 input tokens before cancellation. Desktop visual and interaction checks passed in the Codex in-app browser; Microsoft Edge and mobile viewport checks remain open. [Sanitized live evidence](evidence/testproj-browser-coding-20261012.json).

2026-10-13 live browser coding checkpoint: the local xMind browser completed a DeepSeek `deepseek-flash` run on the existing TestProj Model Zoo workspace. The run changed only `model-zoo/frontend/src/style.css`, then used xMind's trusted PowerShell profile to execute `npm.cmd run build`; Vite 6.4.4 transformed 36 modules and exited 0 in 700 ms with no stderr. The selected-run UI showed provider-reported 43,437 input / 785 output tokens and measured timing. The live catalog at `127.0.0.1:5173` returned HTTP 200; its accessibility tree exposed the demo-data disclosure, filters, cards, pagination and detail links. The current combined browser and VS Code client contracts pass 421/421. This establishes one live browser write/build loop, not live VS Code editing or full coding parity. [Sanitized evidence](evidence/testproj-browser-coding-20261013.json).

2026-10-13 VS Code coding acceptance: the actual Extension Development Host used backend root `D:\CantorAI2026\TestProj`. Its first pytest command found the stale PATH xlang3 and stopped before test collection; the exact compatibility-branch xlang3 executable plus explicit pure-Python package paths passed all 9 API tests. A follow-up Claude run made an approved, diff-reviewed insertion of three mobile touch-target rules in `model-zoo/frontend/src/style.css`, then built the Vue app through xMind's native process tool (Vite 6.4.4, 37 modules, exit 0, 683 ms). The source and emitted CSS were independently checked, and no other frontend source file changed. The combined browser/VS Code contracts pass 421/421. This validates one live VS Code edit/build and pytest workflow; phone viewport rendering and broad coding parity remain open. [Sanitized evidence](evidence/testproj-vscode-coding-20261013.json).

2026-10-13 native MCP resource-catalog checkpoint: the local xMind Server now enumerates resources from trusted configured MCP peers, follows bounded native pagination, rejects invalid/duplicate resource URIs, requires local authentication, and suppresses a peer catalog that reflects a configured credential. The CLI exposes `mcp-resources`. The actual-server contract exercises a two-page stdio peer, missing authentication, rejected client-supplied cursors and credential reflection; the six focused MCP wire/client/HTTP/agent contracts pass after a Release rebuild. [Focused CTest output](evidence/native-mcp-resource-catalog-ctest-20261013.log). OpenCode's location filter/response shape and rendered editor catalog UI remain incomplete; the API inventory therefore marks this as a partial source mapping, not parity.

2026-10-13 MCP resource UI checkpoint: Browser and VS Code Settings now offer an explicit **List resources** action against the authenticated local catalog route. Results are schema-validated and bounded, and peer metadata is rendered as text only. The full browser/VS Code client suite passes **432/432**; the browser asset build includes the control and route. [Full test output](evidence/mcp-resource-browser-vscode-tests-20261013.log). Live rendered UI acceptance and OpenCode location-query/response-schema parity remain unverified.

2026-10-13 MCP resource-template checkpoint: both native transports now issue `resources/templates/list`; C++ validates, paginates and bounds URI templates and the native catalog includes per-server template counts. The unified production `xmind` target now links the catalog library. Release targets `xmind`, `xmind_server`, `xmind_cli`, `xmind_admin`, and the stdio/HTTP client contracts built; the native stdio, HTTP and actual-server catalog tests pass **3/3**, and browser/VS Code client tests pass **432/432**. A live authenticated browser Settings run against the freshly built xMind backend showed **List resources** and returned the expected empty-catalog state with no configured MCP peers. [Native CTest output](evidence/native-mcp-resource-template-focused-ctest-20261013.log), [browser/VS Code tests](evidence/mcp-resource-template-browser-vscode-tests-20261013.log). OpenCode's location query/exact response shape and a visually accepted populated editor catalog remain open.

2026-10-13 A2A inline text-file checkpoint: xMind `3108793` adds bounded native decoding for current A2A 1.0 `raw` FileParts and legacy 0.3 `fileWithBytes`. It accepts UTF-8 `text/plain`, preserves the original wire part in durable history, and frames decoded text as untrusted model input. Strict base64, NUL and UTF-8 checks reject malformed/binary data; remote URLs and unsupported media types are rejected before admission. The Visual Studio 18 Release server build succeeded and the actual-server task-control, message-admission, streaming and official A2A JS SDK v1 contracts passed **4/4**. Provider responses are synthetic; non-text attachments, task continuation, references, push notifications, remote delegation and full external-peer interoperability remain open. [A2A implementation limits](native-a2a-v1.md), [official protocol specification](https://github.com/a2aproject/A2A/blob/main/docs/specification.md).

2026-10-13 native A2A human-input continuation checkpoint: both local A2A wires can continue a task only when its clean native owner has durably paused on exactly one pending human question. The adapter exposes that question in `tasks/get`; the follow-up answer, stable message ID, original A2A payload, task-history entry and event are committed through xlang3 SQLite before the same owner is woken. Clean pauses are re-adopted at native startup, and the actual AgentService contract now closes and reopens the SQLite backend before continuing one of them. Exact retries reuse the answer once; changed-payload reuse conflicts. This adds schema 18 with a guarded migration and refuses to erase continuation rows during test-fixture downgrades. Affected Visual Studio 18 Release targets built, and **13/13** focused A2A, AgentService (including backend reopen), planning HTTP, owner and migration contracts passed. Provider replies in these contracts are synthetic. Multiple pending questions, arbitrary active-turn messages, `referenceTaskIds`, remote delegation and full external-peer interoperability remain unsupported. No general xlang3 optimization was included; xlang3 changes remain scoped to concrete xMind dependencies. [A2A continuation limits and contract](native-a2a-v1.md).

2026-10-13 CLI durable session inbox checkpoint: while a CLI turn streams, ordinary prompt text is now persisted through the same native SQLite inbox used by the browser and VS Code. When the active turn reaches a terminal state, the CLI admits queued prompts in order using the ordinary run route; transactional inbox claiming records the run link and exactly-once history. The Release actual-server agent HTTP/CLI contract passes **1/1**, including queueing during held inference, cancellation, automatic follow-up admission and history verification. Inference is synthetic. No general xlang3 optimization was included. [Focused test log](evidence/native-cli-inbox-ctest-20261013.log), [inbox behavior](native-session-inbox.md).

2026-10-13 A2A related-task reference checkpoint: both A2A wires now accept bounded `referenceTaskIds` for completed root A2A tasks in the same local context. Missing/internal tasks are hidden as not found, and active, self, duplicate, malformed or cross-context references fail before admission. References are retained in per-task protocol history and passed with the next user message while reusing that context's durable conversation. Visual Studio 18 Release `xmind_server` rebuild succeeded; actual-server legacy and official SDK v1 HTTP contracts pass **2/2** with synthetic provider replies. Scope remains same-context/local only; remote retrieval and cross-context delegation are not included. No xlang3 changes were needed. [A2A behavior and limits](native-a2a-v1.md).

2026-10-13 reusable agent identity checkpoint: trusted YAML definitions now carry a bounded display `name` and `description` through the native catalogue, session-agent snapshots and version-2 archive import/export. Older definitions and archives default those fields to the ID and empty description. Browser/VS Code agent pickers show the authored name and description while the backend still enforces model and tool bindings. Release native agent service/HTTP contracts pass **2/2**; VS Code client/renderer contracts pass **337/337**. The pinned OpenCode source inventory was regenerated; operation-level parity remains unclaimed. No xlang3 source changes were needed. [Agent authoring](authoring.md), [current parity gaps](PARITY.md).

2026-10-13 native CLI agent metadata checkpoint: the HTTP agent catalog and selected-session snapshot now carry authored names/descriptions, and the console CLI accepts both those current five-field records and legacy three-field records. The actual graph-service HTTP/CLI and agent HTTP contracts pass 2/2 after Release rebuild; this verifies the previously uncovered CLI consumer path. No xlang3 changes were needed.

2026-10-13 per-session model selection checkpoint: xMind now stores each conversation's selected model together with the provider profile and route that resolved it, using revision-checked SQLite updates; forks inherit the selection. The browser and VS Code restore it only when the active profile still matches that provider route, and older backends safely omit the feature through an explicit health capability. Visual Studio 18 Release targets built; focused persistence, migration, owner and provider HTTP contracts pass **10/10**, and the complete browser/VS Code client suite passes **447/447**. No xlang3 source changes were needed. Provider variants, per-request settings and broader rendered/live-provider acceptance remain open.

2026-10-13 per-session model admission checkpoint: the native HTTP server now resolves a missing run model from the session's provider-bound SQLite selection for both single-agent and graph-agent execution. It rejects stale explicit model choices and requests without provider admission before creating run records; selected model IDs are validated against the active native provider route, and profile-bound runner paths accept only those runtime-validated overrides. The actual-server OpenAI wire contract proves the selected model reaches the synthetic peer for one single-agent run and both graph nodes, with provenance and conflict checks passing **1/1**. No xlang3 changes were needed; CLI exposure for choosing a per-session model remains open.
2026-10-13 rendered TestProj Saved Models acceptance: the local Model Zoo browser saved a model from page 1, verified the URL/localStorage state across reload, then saved EfficientNet-B0 on page 2 and confirmed the Saved view found it on page 1 after reload. Both test saves and the filter were cleared afterward. The API/backend and demo data were untouched; broader filter-combination coverage remains open. [Evidence](evidence/testproj-saved-models-live-20261013.json).

2026-10-14 automatic outbound A2A reconnect: the native registered-agent tool now resumes a streamed task after a transport interruption only when it has a remote task ID and an explicit event cursor committed to that operation's owning run. It resumes from the last durable cursor at most twice within the same run deadline and never resends the original task. Missing-cursor/task cases, cancellation, or exhausted reconnects stay uncertain for manual inspection. The actual dynamic-plan contract drops the initial A2A 1.0 stream after event 1, verifies automatic continuation through final event 2, then closes/reopens xlang3 SQLite and verifies a later subscription resumes at event 2 without another send. A separate no-cursor disconnect case proves the operation remains uncertain and does not resubscribe. Release build passed and the A2A client, tool-registry and dynamic-plan CTests passed 3/3. Peers and model replies are synthetic; the native transport, approval, journals and restart are actual. [Test log](evidence/native-a2a-auto-resubscribe-focused-ctest-20261014.log).

2026-10-15 native per-agent deny-policy checkpoint: named agents can add validated `deny` rules by native tool name and workspace-relative glob. Rules advance agent revisions and persist in xlang3/SQLite session snapshots, archives, delegated profiles and graph nodes. Before dispatch, the C++ runner checks resource paths, conservatively rejects directory enumeration that may expose denied paths, and checks every prepared patch target. Process tools can only be denied broadly because their filesystem access is not limited to their working directory. The actual native runner contract confirms a matching read receives an `agent_permission_denied` tool result and no successful tool event; graph, AgentService, HTTP/archive and VS Code archive contracts exercise the transported policy. OpenCode `allow`/`ask`, saved grants and request APIs remain unimplemented; those effects are rejected by profile validation, not treated as supported. This checkpoint does not claim full permission parity.

2026-10-15 interactive CLI workspace-command acceptance: the CLI already resolved `/commands` and `/command-id arguments` against the current authenticated workspace catalogue and submitted invocations through the session command endpoint; the documentation had incorrectly said this was missing, and no end-to-end CLI contract covered the path. The updated native skill/catalogue contract launches the real CLI against the native server, verifies catalogue listing and ordinary run admission, and checks that the backend-expanded Markdown prompt is persisted as the user turn. Provider transport points to an intentionally unreachable synthetic endpoint; the test proves CLI dispatch and prompt persistence, not inference or a successful model response.

2026-10-15 ordered per-agent allow/deny rules: agent profiles now accept ordered `allow` and `deny` effects. For a direct tool/resource pair, the last matching agent rule determines whether that agent-level restriction applies; a later allow cannot widen the tool allowlist, workspace membership, native authorization or effect approvals. Directory enumeration remains fail-closed when any denied descendant may be exposed, and `run_process` remains a broad `*` resource because process file access is not workspace-confined. The graph contract checks ordered glob precedence, and the actual C++ agent-runner contract checks both a denied read and a read enabled by a later matching allow. Interactive `ask`, saved grants and request APIs remain unimplemented.

2026-10-15 one-shot per-agent `ask` rules: ordered policy definitions now accept `ask`. A matching workspace read publishes a durable operation request with the exact run, tool and arguments; the native runner does not dispatch the read until the controller approves it. Approval executes the read once, records its observed result on that operation and continues the model/tool loop; denial returns a denied tool result without dispatch. Enumeration asks conservatively cover directories that may expose a matched descendant. The Release graph and AgentRunner contracts verify rule persistence/glob matching plus actual allow/deny controller decisions and held-before-approval behavior. Saved grants and session-wide permission request APIs remain outstanding.

2026-10-15 xlang3 runtime compatibility promotion: the xMind-required native-package loader fix and Windows long-path-aware CLI host were fast-forwarded from `xmind-required-compat` to `CantorAI/xlang3` `main` and pushed at `fcecf499`. That commit is the exact runtime revision already used by the recorded TestProj acceptance: xlang3 loaded Pydantic Core from the deep managed-module path and passed all nine backend tests. No broader xlang3 optimization or CPython fallback was introduced. A fresh xMind browser profile also loaded the saved Claude provider and displayed its 13 account models without asking for another key; the UI reports that the key remains in local xMind. The direct process invocation without the managed module path is not the accepted test path; use xMind's configured xlang3 process profile for project commands. [TestProj runtime evidence](evidence/testproj-xlang3-long-runtime-path-20261015.json).

2026-10-15 session-scoped permission request API: the native backend lists up to 256 pending approval operations across every run in a session and returns an explicit bound error above that limit. A reply must identify the owning session and an `awaiting_approval` operation; it then settles the same durable allow/deny journal used by the per-run UI. The real compiled xlang3/SQLite server contract verifies listing, denial, rejection when a different session presents the operation ID, unchanged file bytes after denial and removal from the pending list after settlement. Release build and `native_agent_edit_http_contract` passed; browser route allowlist tests pass **114/114** and VS Code client tests pass **356/356**. This adds request discovery and replies, not reusable permission grants or session-policy enforcement. [Permission parity gaps](PARITY.md).

2026-10-15 session-wide approval review in browser and VS Code: both clients now fetch the selected conversation's bounded pending-request list, surface pending approvals from older runs through their existing approval cards, identify the owning run, and refresh the list while that conversation remains open. A decision on another run uses the session-bound reply endpoint after exact operation revalidation; current-run and graph-child decisions keep their existing endpoint. Browser controller and VS Code extension-host contracts verify older-run display and reply routing. Saved grants, session policy enforcement, and global/location request listing remain open.

2026-10-15 TestProj Model Zoo rendered browser acceptance: the Vue/Vite page loaded in the browser against the live Model Zoo API hosted by xlang3. The rendered view showed the filter panel, catalog hero, seeded-data disclosure, result cards and pagination. Clicking “Browse models” focused the search field; “View results” navigated to `#catalog-results` and brought the result cards into view. The API health endpoint returned `ok`, and catalog stats reported 12 seeded models across 10 task types. This verifies the requested TestProj browser check only; the data is illustrative and this result does not establish xMind feature parity. [Acceptance record](evidence/testproj-model-zoo-live-browser-20261015.json).

2026-10-15 TestProj browser coding and build acceptance: the xMind browser client used the saved OpenAI profile to review and apply a single-file accessibility patch in `ModelCard.vue`, then invoked the native process operation for `npm.cmd run build` from `model-zoo/frontend`. The xlang3 process profile correctly rejected `npm` as an xlang source file; the configured PowerShell profile ran the Node build successfully, with Vite 6.4.4 transforming 37 modules and exiting 0. The generated `dist` assets were present after the operation. This verifies one real browser-client edit/review/build path, not complete coding or parity. [Acceptance record](evidence/testproj-xmind-browser-coding-20261015.json).

2026-10-15 named-agent permission visibility: ordered per-agent permission rules now remain present in `AgentDefinitionMetadata`, the native `/v1/agents` catalogue and selected-session metadata. This lets clients inspect the actual persisted policy used by the selected agent instead of seeing an empty/omitted policy in list or selection views. The VS Code picker hover text shows the ordered rules, capped at four entries in the tooltip. The actual compiled xlang3/SQLite server plus admin/CLI HTTP contract passes `native_agent_http_contract`; the native AgentService contract passes `native_agent_service_contract`; the complete VS Code extension/client suite passes **361/361**. Rules omitted by older server revisions remain valid for existing clients. This does not claim `agent.list` OpenCode operation parity or complete permission semantics.

2026-10-15 multi-branch nested human-wait current-source acceptance: rebuilt the Release repository, engine and authenticated planning HTTP contracts; CTest passes 3/3. The actual xlang3/SQLite owner pauses two nested branches under one durable root wait set, restarts with both exact paths, accepts one answer while preserving the unanswered sibling, re-pauses, and completes both original signed calls exactly once. The A2A path rejects ambiguous/stale sibling selection. Browser controller tests pass 64/64 and VS Code planning client tests pass 11/11; these are synthetic client contracts, not rendered or installed-client acceptance. Current Release result and source/binary digests are in [multi-branch evidence](evidence/native-dynamic-plan-multibranch-ctest-20261015.json). Reverse/concurrent answers, cancellation races, expiry/uncertainty and repeated-segment budget/context cases remain open.

2026-10-15 active-session UI acceptance: the native server advertises its active-session map and the browser and VS Code conversation selectors show `Running · <title>` for sessions executing in another view. Status refreshes every five seconds and the picker updates only when the selection, catalogue or active state changes; rename continues to use the saved title. The actual Release xlang3/SQLite server contract verifies authenticated empty/active/settled states, and rendered product-script contracts pass 121 browser and 368 VS Code tests. This improves cross-view run visibility but does not claim exact OpenCode process-owned active-run semantics. [Acceptance evidence](evidence/active-session-indicator-20261015.json).

2026-10-15 conversation model selection audit correction: xMind already persists provider-bound model choices per session in SQLite, restores them, uses them for single-agent and graph runs, rejects stale admission, and returns to the active profile default when cleared. Native runtime/CLI contracts pass 2/2; browser model selector passes 1/1; VS Code session-model client passes 2/2. This corrects the stale inventory description of per-run-only selection, while leaving OpenCode wire and response parity unclaimed. [Acceptance evidence](evidence/session-model-selection-20261015.json).

2026-10-16 native MCP workspace-root continuation: the C++ stdio and HTTP MCP clients answer modern `roots/list` requests from the admitted workspace membership, including attached folders, echo opaque continuation state and retry with a fresh request ID under a ten-round bound. The registry reopens every folder and confirms the primary root. Agent, graph, dynamic-plan and idle-context MCP setup pass the exact admitted folder set. Unsupported requests such as elicitation remain safely unresolved. A real stdio peer checks primary and attached roots in `inputResponses`; Release `ALL_BUILD` passed, the effect contract passed **1/1**, and the broader MCP/agent/graph set passed **25/25**. OpenCode and LiteLLM remain design/coverage references only; this checkpoint does not define compatibility requirements. Human-input UI and durable continuation remain open. [Evidence](evidence/native-mcp-admitted-multi-roots-20261016.json).

2026-10-17 webpage coding follow-up: the xMind browser client applied the approved CSS-only hero adjustment in TestProj, then invoked `npm.cmd run build` through the xMind workspace process tool. Vite 6.4.4 transformed 37 modules and exited 0 in 711 ms. The xlang3-backed API reported health `ok` and 12 catalog models, and the built page rendered without API errors. The model then proposed redundant full-file replacements, so the run was stopped after the successful build. The exact run state, source/output hashes and limits are recorded in [the acceptance evidence](evidence/webpage-testproj-css-hero-20261017.json).

2026-10-17 bounded webpage coding retest: a fresh TestProj conversation applied one reviewed CSS-only hero patch, ran `npm.cmd run build` through the saved PowerShell process profile, and completed its final response without proposing follow-up edits. Vite 6.4.4 transformed 37 modules and exited 0; the built page rendered the hero, 12-model catalog, filters and model cards with API data visible. The patch and process operations, before/after hashes, model timings and known context cost are recorded in [the acceptance evidence](evidence/webpage-testproj-bounded-coding-20261017.json). This closes the specific post-build continuation failure for a bounded prompt; repeated-task reliability, mobile visual review and full redesign remain unverified.

2026-10-17 focused webpage source-read acceptance: the local browser UI served HTTP 200, reused the saved OpenAI Responses profile, discovered 20 models, and completed a bounded TestProj request. The model searched `HomeView.vue` for `goToPage`, read only 35 lines around the implementation, and answered when pagination returns without editing a file or starting a process. Three model calls used 16,385 input and 169 output tokens; these totals describe this read-only task and are not directly comparable with edit/build runs. The staged local runtime inventory was hash-verified but is not release-accepted. A fresh VS Code visual recheck could not be counted: Windows capture returned a black image and no accessibility text, so this records neither a rendering pass nor a product failure. Prior live VS Code edit/build acceptance remains in [the dual-client record](evidence/live-coding-acceptance-20261016.json). [Focused-read evidence](evidence/focused-read-webpage-live-20261017.json).

2026-10-17 VS Code client recheck: the current xMind 0.1.13 extension suite passes **390/390** with Node 22.19.0. The live Extension Development Host remains registered to the opened TestProj folder and has a native xMind view ready record. This verifies current extension logic and workspace attachment, while live pane rendering remains inconclusive because the same Windows capture session returned a black image for both VS Code and Edge. The Oct 16 live VS Code edit/build acceptance remains the rendered coding evidence. [Current client and host evidence](evidence/vscode-client-suite-and-live-host-20261017.json).

2026-10-17 xlang3 pytest through the webpage process tool: on the focused xlang3 branch `b4a53192`, the TestProj browser run selected the managed `xlang3` process profile and ran `xlang3.exe -B -m pytest -q` from `model-zoo/backend`; it exited 0 with **9 passed** and the existing Starlette/AnyIO deprecation warning. The same command passed when launched directly with the xlang3 Release executable, and the branch's `cached_property_inline_method_fallback` fixture matched its expected output. No CPython executable was invoked. This verifies one real xMind process-tool-to-xlang3 Python test path; it does not claim general package compatibility or merge the focused xlang3 branch to main. [Runtime and live-run evidence](evidence/testproj-xmind-ui-xlang3-pytest-live-20261017.json).

2026-10-17 webpage coding retest: xMind applied approved Vue edits in TestProj, but its broad redesign run ended with `execution_budget_exhausted` before starting the build after repeated full-file proposals. A separate xMind run executed the configured PowerShell profile and passed `npm run build` (Vite 6.4.4, 37 modules, exit 0); the independent build and browser search/reset/detail checks passed as well. The current VS Code extension suite passed 390/390, and its TestProj Extension Development Host activation is present in logs; a new live VS Code edit was not verified. The split-run build validates process execution but does not turn the failed combined run into a success. [Evidence and limitations](evidence/testproj-webpage-redesign-run-20261017.json).

2026-10-17 native coding workflow guidance: trusted workspace coding requests now ask the model to use bounded reads, batch related diffs, avoid repeated whole-file edits, and stop after requested checks pass. Release `xmind_server` build and `native_agent_edit_http_contract` (1/1) passed. This is runtime instruction coverage only; a new live-provider run is still needed to establish whether it reduces coding-run churn and budget failures. [Contract evidence](evidence/native-coding-workflow-guidance-20261017.json).

2026-10-17 bounded webpage code-and-run smoke: a new TestProj browser conversation created the approved `xmind_coding_smoke.py` with one file operation, selected the saved `xlang3` process profile, and executed `xlang3 -B xmind_coding_smoke.py` with exit 0 and `Hello, TestProj!` output. The VS Code extension suite reran at 390/390. This is live webpage coding/process evidence; the run used an older hosted runtime, so it does not verify the latest native coding guidance, and no new live VS Code edit was performed. [Evidence](evidence/webpage-xlang3-coding-smoke-20261017.json).

2026-10-17 current-source webpage coding acceptance: staged and hash-verified a fresh browser runtime from xMind revision `961968e` and paired xlang3 SDK. The local webpage fetched 20 OpenAI models from the existing saved configuration, approved one precise `xmind_coding_smoke.py` edit, and ran the configured xlang3 executable with exit 0 and `Hello from current xMind, TestProj!`. This live provider run exercises the current native coding guidance; repeated large-task reliability remains open. VS Code tests pass 390/390, but no live VS Code edit was performed this turn. [Evidence](evidence/current-source-webpage-coding-20261017.json).

2026-10-17 current-source xlang3 tool worker: built the unified `xmind.exe` from current `main` and ran a separate child in private worker mode. A C++ xlang3 LRPC probe verified unauthenticated rejection, an authenticated per-process capability handshake, a real fixture transform (`{"result":"worker:xMind","count":5}`), and replay rejection. The child was retired after the probe; no provider credential or production key was used. This verifies the existing Python-callable worker only; process-bound `AgentRunner`, durable crash recovery and client inspection/control remain open. [Exact revisions, hashes and result](evidence/agent-worker-lrpc-current-source-20261017.json).

2026-10-17 current-main client verification: the VS Code extension suite passed **392/392**, and the browser client/server-adapter suite passed **136/136** on Node 22.19.0. The browser suite must run from the xMind repository root because its fixtures use root-relative paths. This is source-level client coverage, not fresh live editor installation or browser acceptance. [Test record](evidence/client-tests-current-main-20261017.json).
