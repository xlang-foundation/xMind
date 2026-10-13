# OpenCode 2 feature baseline

The [xMind OSS specification](architecture.md) covers the local C++/xlang3
backend, SQLite, single and graph agents, native providers, MCP/A2A, CLI,
browser UI and the VS Code extension. Local/Nexus connection profiles use the
shared command/event protocol. Nexus owns the private team server, PostgreSQL,
WebRTC and standalone Electron IDE; these are excluded from OSS delivery.

OpenCode is a behavior reference only. The Python/FastAPI agent prototype and
original xlang service/plugin tree were removed at the user's request. Git
history preserves them, but prototype checks and source mappings do not count
as native delivery. See [native ownership](NATIVE_ARCHITECTURE.md) and
[cleanup](cleanup.md).

Baseline: `anomalyco/opencode` tag `v2.0.26`, commit
`9b4ec5714d481559990db0a816d5dec19541a814`, verified against the remote tag and
checked out locally. [Pinned source](https://github.com/anomalyco/opencode/tree/v2.0.26),
[release](https://github.com/anomalyco/opencode/releases/tag/v2.0.26), and
[V2 documentation](https://opencode.ai/v2/docs) checked 2026-10-10. The docs
track the current V2 surface and can move independently of the source tag;
source-defined API contracts take precedence for this inventory.

## xlang3 tool worker and staged VS Code runtime checkpoint (2026-10-13)

Trusted xlang3 tool registrations now select `execution: in_process` or
`execution: worker`; worker placement launches the unified `xmind` binary for
one tool invocation and returns bounded JSON over the selected xlang3
`lrpc:` shared-memory transport. Release contracts passed the worker tool
call, cancellation, trusted-config round trip, and the actual AgentRunner/model
tool loop. The VS Code extension suite passed **321/321**.

The production native-runtime staging and verification code packaged the
real built `xmind.exe`, xlang3 runtime, pure standard-library sources, and
`worker/agent_worker.py` into a local 1,878-file staged runtime. The staged
binary then launched its worker and passed the real shared-memory
call/cancellation contract. This is local package evidence, not an accepted
hosted bundle or installed VSIX test; see
[staged-runtime evidence](evidence/native-worker-staged-runtime-local-20261013.json).
The operation inventory was regenerated from the pinned OpenCode `v2.0.26`
source after these changes. It still counts **141 operations, 32 reviewed
native source candidates, and zero verified operation-level parity**. Worker
placement for a full agent, a reusable worker pool, resource limits and OS
sandboxing remain open; this checkpoint does not establish broad coding parity.

## Live browser coding acceptance (2026-10-13)

The local xMind browser completed a real DeepSeek-assisted stylesheet task in
the TestProj Model Zoo workspace. It applied one scoped `style.css` change,
then invoked the trusted native PowerShell process profile to run
`npm.cmd run build` from `model-zoo/frontend`; Vite 6.4.4 transformed 36
modules and exited 0 in 700 ms. The browser displayed provider-reported input
and output tokens and measured model timings. A separate accessibility-tree
inspection of the live catalog returned HTTP 200 and found its demo-data
disclosure, filters, model cards, pagination and detail links. The combined
browser and VS Code contract suites pass **421/421**. This proves one live
browser write/build flow and client contract coverage; it does not establish
visual-quality review, live VS Code editing, broad project coverage or
operation-level OpenCode parity. See
[`testproj-browser-coding-20261013.json`](evidence/testproj-browser-coding-20261013.json).

## Live VS Code coding acceptance (2026-10-13)

The actual VS Code Extension Development Host admitted `D:\CantorAI2026\TestProj`
as its backend workspace. The default PATH-selected `xlang3.exe -m pytest -q`
failed before collection because PATH selected a stale runtime without pytest.
Running the xlang3 compatibility-branch executable explicitly, with its module
directory and TestProj's pure-Python package target on `PYTHONPATH`, passed all
9 backend tests (one upstream Starlette/AnyIO deprecation warning). A subsequent
Claude run in the VS Code panel proposed a broad stylesheet rewrite; that
proposal was denied. Its follow-up diff added only three mobile touch-target
rules inside the existing `max-width: 640px` block, and the approved change
passed the TestProj Vite build (37 modules, 683 ms, exit 0). The changed source
and emitted CSS were verified from disk. Combined browser/VS Code contracts
pass **421/421**. This proves a live VS Code write/build loop and explicit
xlang3 branch pytest path; a mobile viewport review, broader coding coverage
and OpenCode parity remain open. See
[`testproj-vscode-coding-20261013.json`](evidence/testproj-vscode-coding-20261013.json).

## Latest verified native coding checkpoint (2026-10-11)

Single-agent and graph runs now route reads, process execution, file creation,
file edits and `apply_patch` through the exact multi-folder workspace snapshot
captured at admission. Graph `create_file`, `edit_file` and each patch-file
proposal are still approved by the controller; their durable journals bind
the workspace revision, selected folder and native root identity. Each patch
call targets one selected folder and cannot span roots. Process profiles can
be explicitly marked trusted to record a durable profile-scoped grant instead
of prompting for each process. The child receives the ordinary Windows
toolchain environment needed to resolve `node`/`npm`, while unrelated inherited
provider secrets remain excluded.

Actual AgentRunner and GraphRunner contracts use synthetic model peers but
launch real PowerShell processes and verify effects in only the selected
secondary folder. The graph contract also approves and verifies secondary-
folder creation, editing and patching; the agent contract verifies a model-
issued patch through approval and durable continuation. The three focused
process/agent/graph contracts pass ([CTest output](evidence/native-multifolder-effects-local-20261011.log));
the six focused agent/graph/patch contracts also pass
([patch CTest output](evidence/native-multifolder-patch-local-20261011.log)).
The unified `xmind` RelWithDebInfo executable builds. Separately, the live
TestProj browser run executed its real Vue/Vite `npm.cmd run build` with exit
code 0. These results establish local native and browser-build behavior, not
live-model/VS Code multi-folder acceptance.

The browser Git panel now offers every registered workspace folder and scopes
repository initialization, status, review-base detection and diffs to the
selected `folder_id`. Its secondary-folder contract verifies selection and
routing for each of those operations. Multi-root language-server selection
is covered by the native and VS Code host contracts. AgentRunner now also has
a synthetic provider-driven turn using real Rust Analyzer that queries the
admitted secondary root and rejects a folder attached during the run. Rendered
multi-folder VS Code acceptance remains open.

The pinned source inventory has been regenerated from the exact v2.0.26
schema: **141 operations, 32 native source candidates, 109 unmapped, and zero
verified operation-level parity**. A source candidate means reviewed code
exists; it is not feature parity. Remaining work includes end-to-end
multi-folder CLI and installed VS Code acceptance, broader provider/model
conformance, and the substantial unmapped
OpenCode coding/runtime surface. Private Nexus server,
PostgreSQL, WebRTC and Electron remain outside the xMind OSS scope.

The native session layer now supports full and before-message conversation
forks through the actual xlang3/SQLite repository, versioned HTTP server and
CLI. Focused contracts verify exclusive cutoffs, stable `msg_<seq>` IDs,
lineage, transcript immutability, agent/skill state copies and restart recovery.
The inventory marks `session.fork` as a partial native source candidate. Browser
and VS Code now expose guarded per-message fork controls, backed by focused
controller/renderer contracts. Full OpenCode Session.Info parity remains open;
see [native session fork](native-session-fork.md).

## Workspace/browser baseline checkpoint (`4831636`, 2026-10-11)

The browser exposes separate **New workspace** and **Add folder** operations.
Creation starts from one selected folder as the primary folder; later additions
stay in the same workspace and keep its active session. The native launcher now
builds its ignored browser assets from the checked-out source at startup, so a
stale generated asset directory cannot hide or revert those controls. Existing
managed single-folder profiles that have not yet been registered retain a
verified primary-folder membership snapshot; once registered, runs capture the
persisted membership revision.

The full Release CTest gate passed **133/133 in 590.37 seconds**, including
local-profile restart, actual local-view graph admission, and workspace
registry contracts. The combined browser and VS Code suites passed **391/391**;
the startup contract additionally launched the adapter with an empty asset
directory and verified the served page contains both current workspace
actions. The source-generated OpenCode inventory was refreshed against the
pinned `v2.0.26` schema at that checkpoint: **141 operations, 31 native source candidates, 110
unmapped, and zero verified operation-level parity**. These checks do not
establish full coding workflows, installed-editor acceptance, worker/IPC
placement, or OpenCode parity. See the [multi-folder workspace contract](native-multi-folder-workspaces.md)
and [worker/IPC acceptance gap](native-ipc-worker-design.md).

## Follow-on workspace UI and coding-tool checkpoints (`b2c4087`, `499dba2`)

The browser now keeps the active workspace and conversation as its primary
surface: the workspace file canvas is on the left, the agent session is on the
right, and workspace switching/folder membership are configured from the
top-bar dialog. The browser workflow was exercised by switching between two
local workspaces and back. Provider discovery used the four locally saved
credentials without exposing them to the page; a live OpenAI response displayed
provider-reported token counts and measured timings. The browser contract suite
passed 91/91.

The native single-agent coding loop now resolves read/list/search/glob and
`edit_file`/`create_file` calls against the run's captured workspace membership.
Attached-folder write approvals retain the workspace revision, folder ID and
root identity; uncertain edit recovery resolves the same admitted snapshot.
Windows Release build and five focused native contracts passed (workspace
tools, agent runner, graph runner, create executor and edit executor). The
secondary-folder edit fixture verified the primary file remained unchanged.
Commits `b2c4087` and `499dba2` are pushed to
`checkpoint/native-persistence-m1`.

The OpenCode inventory was regenerated from the unchanged pinned
`v2.0.26` source commit after these changes: **141 operations, 31 native source
candidates, 110 unmapped, and zero verified operation-level parity**. This
checkpoint does not establish rendered multi-folder acceptance in the browser
or VS Code. Cross-root `apply_patch`, graph write nodes, process execution,
client-visible Git folder selection, language-server routing and complete
CLI/VS Code coding workflows remain incomplete. The source hash inventory is in
[`OPENCODE_API_INVENTORY.json`](OPENCODE_API_INVENTORY.json).

## Latest native coding/runtime checkpoint (2026-10-11, VS Code package `0.1.8`)

Named YAML agent definitions and graph agent nodes carry optional tool
allowlists. The native loop filters the authorized catalogue for single-agent
sessions and each graph agent independently; caller graph nodes cannot
override backend-pinned bindings. Definition revisions rotate when bindings
change, and session snapshots retain them.

Trusted xlang3 programming mode declares agents, graphs and callable tools
through the native `xmind.agent`, `xmind.graph` and `xmind.tool` API. The C++
AgentRunner contract authors a callable with xlang3, imports its declaration
into xlang3-backed SQLite, reloads it, then executes the actual model/tool
continuation using that persisted registration. The authoring, tool-catalogue
and AgentRunner contracts passed locally (3/3); the inference peer is
synthetic, while the xlang3 callable and durable conversation are real. This
does not sandbox trusted code, provide hard interruption, or establish full
programming-mode catalog replacement and graph execution parity. See the
[authoring contract](authoring.md) and
[execution evidence](evidence/agentflow-xlang-tool-runner-local-20261011.json).

The Release source at `7acf21f` passed the preceding full native CTest gate,
**132/132** in **604.75 seconds**; exact runtime/package scope is recorded in
the [0.1.7 package evidence](evidence/vscode-0.1.7-package-preview.json). The
new managed-view process-profile wiring builds in Release. Its focused native
local-profile contract verifies trusted configuration import, refusal of an
implicit change, protected backend-owner handoff, and SQLite session/history/
graph preservation across restart. The VS Code suite passes **298/298** and
the browser suite **85/85**. The complete native gate and actual populated
VS Code process-run acceptance have not been rerun on this checkpoint.

Managed browser and VS Code launchers now pass a trusted `--process-config`
path to `xmind view`; native code reads and imports it, while the browser and
extension never receive executable bindings. The browser uses the repository
`.config/processes.yaml` when present; VS Code development uses that file and
packaged installs can set `agentflow.processConfigPath`. A fresh Windows
backend now seeds a built-in trusted PowerShell profile (system `cmd.exe`
fallback) only when no registry/config exists; shell commands run without a
per-command approval prompt. Saved custom and explicit empty registries remain
unchanged.
The native foreground/background executor now has actual Windows-child,
approval, event streaming, 64 MiB paged-output artifact, restart uncertainty,
and synthetic-model HTTP/CLI continuation contracts. The shared browser and
VS Code adapters have bounded output renderer/host contracts. An actual
approved process run from each managed client, shell-profile usability,
terminal lifecycle acceptance and full project coding workflows remain open.

VS Code also has a confirmation-gated **xMind: Apply Trusted Runtime
Configuration** command. It reconnects through Native with the one-shot
`--upgrade-backend` request so edits to trusted process-profile contents can be
imported while preserving saved data. The command and native argument handoff
are covered by the host suite. The rebuilt 0.1.8 VSIX was installed locally;
invoking the command against a real backend and reviewing its rendered prompt
remain unverified.

The pinned OpenCode inventory remains **141 operations**, with **31** mapped
native source candidates, **110** unmapped, and **zero** operations with
verified parity. Earlier full native, browser and VS Code gates remain scoped
to their cited revisions; they do not establish full parity or current
installed acceptance.

## Current shared-client package and saved-key status (2026-10-11)

VS Code package `0.1.9` adds an explicit Settings indicator when a saved
provider profile is selected: its encrypted key is configured on the local
xMind backend and will be reused without entering it again. The indicator is
derived from saved-profile selection; no credential or credential reference is
sent to the renderer. The actual local browser page showed this status for its
active DeepSeek profile, while its API-key input remained empty. The browser
suite passes **85/85** and the VS Code suite **298/298**. VSIX packaging verified
the 1,899-file native runtime, and the installed extension list reports
`agentflow-local.agentflow@0.1.9`. The shared renderer is updated in both
clients, but a newly rendered VS Code sidebar has not been observed yet. This
UX checkpoint has since been verified by a live browser model-discovery request:
the active DeepSeek profile returned two model IDs from its saved key, and its
selected connectivity run returned `OK`. The secret remained backend-owned;
the other provider profiles were not retested in this check. It does not change
the **zero** operation-level parity count. [Sanitized browser evidence](evidence/provider-key-status-live-browser-20261011.json).

## Interactive CLI run control (2026-10-11)

The C++ console accepts `/cancel` while observing a live single-agent or graph
run and remains attached until the backend emits the terminal event. `/exit`
detaches observation while the backend-owned run continues; ordinary input is
queued for the next prompt. The exact pushed source, `5e964f5`, passes seven
focused Release CTest contracts covering agent cancellation/detach, graph
control/checkpoints, approval prompts, context, skills, HTTP CLI and provider
profiles. This closes those interactive behaviors at the tested CLI contract
scope only; full-screen CLI parity, cross-client run-control parity, hosted
package acceptance and the full native gate remain open. See
[CLI lifecycle details](native-interactive-cli.md#conversations-observation-and-approvals).

## MCP resources and prompts (2026-10-11)

The native MCP registry now discovers resources and prompts alongside tools for
single-agent and graph execution. Each item is exposed through the existing
model tool catalogue and requires durable controller approval before a resource
read or prompt retrieval. Approval binds the configured server revision, peer
identity, exact metadata/schema and resource URI where applicable; successful
results are journaled, and peer-reflected configured credentials are withheld.
Existing MCP tool approval records retain their prior durable binding shape.
Native stdio and HTTP clients implement list/read/get requests.

The Release build and ten focused native CTest contracts pass, including real
stdio peers for modern/legacy protocol behavior, approval/denial, dynamic-plan
compatibility, graph MCP, and a local Streamable HTTP peer. The pinned official
MCP server SDK 2.3.1 now exercises HTTP resource/prompt discovery and retrieval
through xMind's configured client, approval journal and xlang3-backed SQLite;
allow, deny and reopen outcomes are covered. The combined renderer and
browser-controller suite passes **117/117**. Subscriptions, additional HTTP/SSE
behaviors and broad live MCP coding remain open. This is scoped implementation
evidence, not operation-level OpenCode parity.

## Per-agent native tool bindings (`449c085`)

## Per-agent native tool bindings (`449c085`)

Named YAML agent definitions and graph agent nodes can now carry an optional
tool allowlist. The native loop filters the existing authorized catalogue for
single-agent sessions and each parallel graph agent independently; caller graph
nodes cannot override a named profile's backend-pinned bindings. Definition
revisions rotate when bindings change, session snapshots retain them, and a
stale or unavailable tool binding fails before inference. The focused Release
build and four native graph, graph-runner, agent-service and HTTP contracts
passed locally. The initial implementation did not expose callable tools from
the xlang3 programming API; that gap is closed at the scoped level described
above. Broader OpenCode operation parity remains incomplete. [Authoring
contract](authoring.md).

A subsequent live local-browser check fetched saved-credential model catalogues
for Claude, Gemini, DeepSeek and OpenAI (13/11/2/20 models) and restored the
prior DeepSeek selection. The page received model IDs only. This is not an
inference, streaming, or installed VS Code result; see the
[catalogue evidence](evidence/provider-catalogues-live-browser-20261010.json).

The same-machine page's current DeepSeek backend was rechecked on 2026-10-11:
model discovery without an `api_key` field returned two catalogue IDs from its
encrypted saved credential. The VS Code Settings success message now makes
saved-key discovery explicit. The package is installed as 0.1.6, but a fresh
rendered-sidebar result is not yet verified; see the
[scoped saved-key evidence](evidence/provider-saved-key-live-backend-20261011.json).

The local browser was restarted on the verified runtime containing the
provider-YAML synchronization fix. A fresh page connected to the same local
profile store and reported that it fetched two DeepSeek models using the saved
key. The browser received model IDs and a success status only; no credential
was returned to it. This checks catalogue discovery, not live inference or
external-browser/installed-editor behavior. See
[provider-key UI evidence](evidence/provider-key-ui-local-20261011.json).

The rendered browser was then opened as a fresh same-machine view, connected to
the local process without a server-token or provider-key prompt, and used its
saved OpenAI profile for a completed `gpt-5.6-sol` request. The response and
provider-reported usage/backend timings appeared in the sidebar in both the
in-app browser and external Microsoft Edge. The old unlinked `60415` browser tab is excluded from this acceptance. Exact evidence
and limits: [saved-profile UI run](evidence/browser-live-saved-profile-ui-20261010.json).

The current OpenAPI audit contains **141 operations**: **31** have a mapped C++
source candidate, **110** remain unmapped, and **zero** are verified parity.
Native Git initialization, status, branch metadata/listing, review base and diff add six source candidates; they
do not establish full VCS or operation-level parity.
Compared with v2.0.16, v2.0.26 adds five operations: credential creation/listing,
server connect/pair, and VCS initialization. These operation IDs are unchanged
from v2.0.24 through v2.0.26. The audit embeds all **256** pinned
schema components and resolves all **885** JSON references, including each
operation's request and response definitions. Its generation is deterministic
and refreshes native source hashes. A mapping or passing component contract
alone does not prove end-to-end feature equivalence.

### Native Git initialization checkpoint

`POST /v1/vcs/init` now initializes Git inside the C++ runtime's opened
workspace, using the bounded Windows process adapter and no caller-selected
directory or executable. The local owner view can invoke it; concurrent calls
serialize, and an existing repository returns a conflict. The actual xMind
binary contract verified the created `.git` marker and Git's reported root,
authorization, duplicate initialization and unsupported providers. The browser
workspace header and VS Code command palette now expose the operation, bound to
the connected workspace and guarded by confirmation. Browser and VS Code
contracts cover success, cancellation and existing-repository errors. Branches,
diffs and other VCS providers remain unimplemented. See
[local VCS evidence](evidence/native-vcs-init-local-20261010.json).

### Native Git status checkpoint (2026-10-11)

`GET /v1/vcs/status` reports Git changes and per-file added/deleted line counts
from the opened workspace. It runs fixed native Git arguments with bounded
output/lifetime and rejects caller-selected paths. The actual xMind binary and
xlang3/SQLite HTTP contract verified a clean repository, an untracked file with
a space in its path and exact two-line counts, then a committed file changed
with one addition and one deletion; bearer and local-view authorization and
query rejection were also checked. The browser workspace includes a Git status
control, with adapter and rendering coverage in the 81-test browser suite; a
fresh rendered-browser check has not yet been done. VS Code's `xMind: Show Git
Status` command is bound to the opened workspace and lists backend-reported
paths and line counts; its host contract is covered by the extension suite, not
a rendered IDE check. Branch listing, review base, full diffs, rename/binary
behavior and broader VCS equivalence remain incomplete. The console CLI exposes
`xmind git-status` through the same native endpoint, validates the response and
prints JSON; its contract uses an authenticated synthetic HTTP peer, while the
backend contract above exercises actual Git in the opened workspace. See [CLI
status evidence](evidence/cli-git-status-local-20261011.json), [native status
evidence](evidence/native-vcs-status-local-20261011.json) and [VS Code status
evidence](evidence/vscode-git-status-command-local-20261011.json).

The following checkpoint adds `GET /v1/vcs/info` and `/v1/vcs/branches`, with
`xmind git-info` and `xmind git-branches`. Current/default branch metadata and
local refs are read from the same opened workspace through bounded native Git
commands. The native HTTP contract covers unborn and committed local branches;
CLI contracts check authenticated routing and reject malformed replies. The
webpage Git panel renders branch metadata and distinguishes a clean repository
from a workspace without Git; its full browser suite passed 82 tests. Remote
branch search, rename/binary details and full OpenCode VCS parity remain open.
See the [native branch evidence](evidence/native-vcs-branches-local-20261011.json).

### Native Git review diff checkpoint (2026-10-11)

`GET /v1/vcs/base` reports a review base only when the opened branch's remote
default or branch-creation reflog provides one. `GET /v1/vcs/diff` supports
working-tree, branch and committed comparisons, optional explicit base and
bounded context. It returns per-file patches and exact added/deleted line
counts, including untracked files, without accepting a workspace path. The
actual xMind binary/xlang3 contract covers a clean default with no inferred
base, modified and untracked files, a branch created from `HEAD`, committed
comparison, query validation and browser-view authorization. Authenticated CLI
contracts cover endpoint construction and response validation. Patch process
and aggregate output are bounded at 4 MiB. Rename detection, binary UX,
cross-platform acceptance and rendered browser/IDE diff views remain incomplete.
The pinned OpenCode audit now maps **31** native source candidates, with **110**
unmapped and zero verified operation-level parity. See the
[native diff evidence](evidence/native-vcs-diff-local-20261011.json).

### Git diff client checkpoint (2026-10-11)

The webpage workspace view now selects working-tree, branch or committed
comparisons, lists native changed-file counts and renders the selected unified
patch with text-only insertion. Branch/committed comparison automatically uses
the native inferred base, or asks for an explicit branch/commit when none is
available. The VS Code command palette's **xMind: Review Git Diff** follows the
same backend/workspace contract and opens the returned patch in VS Code's diff
language mode. Both clients recheck their selected workspace before displaying
results; browser workspace changes clear retained patches. The production
client contract suites passed **280 VS Code** and **84 webpage** tests. These
are host/DOM contract results, not a rendered external browser or installed IDE
acceptance; side-by-side source comparison and approval/recovery UX remain open.
See the [VCS review client evidence](evidence/vcs-review-clients-local-20261011.json).

### Multi-folder native Git routing checkpoint (2026-10-11)

VCS initialization, status, branch metadata, review base and diff now accept an
optional registered `folder_id`. The C++ runtime rebuilds an immutable
`WorkspaceToolSet` from the current membership snapshot for each operation,
uses the primary folder when the ID is omitted, and rejects unregistered IDs;
requests cannot provide a filesystem path. The provider-profile and legacy
provider runtimes forward the selection to the same native execution platform.
The xlang3-backed HTTP contract initializes Git in a secondary folder, checks
its independent status, verifies the primary folder was untouched, and rejects
an unregistered ID. The Git launcher also stopped duplicating ordinary
inherited environment variables. At this checkpoint the browser Git panel still used the primary
folder. See the [multi-folder VCS evidence](evidence/native-vcs-multifolder-local-20261011.json).

### VS Code and CLI Git folder selection checkpoint (2026-10-11)

`GET /v1/workspace/membership` returns the connected profile's registered
folders to the trusted local VS Code host. The extension uses this membership
for folder QuickPicks and revalidates its workspace ticket before each Git
operation; the selected secondary ID is forwarded to native VCS routes. CLI
Git commands accept optional folder IDs for status/info/branches/base and
`--folder-id ID` for diff; `xmind workspace info` lists those IDs. The actual
local-view/xlang3 integration verified membership after attaching a second
folder. The VS Code host suite passed 304 tests, including secondary-folder
initialize/status/diff selection; all three native CLI Git contracts passed
authenticated ID routing and response validation. Rendered VS Code acceptance and complete multi-folder coding remain open. See
the [Git folder selector evidence](evidence/vcs-folder-selectors-local-20261011.json).

### Browser Git folder selector checkpoint (2026-10-11)

The webpage now lists each registered workspace folder in the Git review
controls. Initialization, status, inferred review base and diffs carry the
selected secondary `folder_id`; late responses are ignored after a folder
switch. Browser contract coverage passed **94/94**. The currently running local
page returned HTTP 200 and served the new selector. This verifies the deployed
page asset is fresh; the secondary-folder requests are exercised by the browser
DOM contract, not by a live Git repository in that browser session. See the
[browser Git selector evidence](evidence/browser-git-folder-selector-local-20261011.json).

### VS Code language-server navigation checkpoint (2026-10-11)

The VS Code extension now registers definition, references and hover providers
for configured language-server languages. Requests use the opened document's
current buffer and authenticated workspace authority; locations outside the
opened workspace are discarded, and stale or cancelled replies are ignored.
The complete extension suite passed **290/290**, including navigation mapping,
hover rendering, workspace filtering and stale-result cases. The real
Rust Analyzer contract now passes symbols, diagnostics, definition, references
and hover through the native client, plus a definition through authenticated
HTTP and browser-view requests. The 2026-10-11 checkpoint adds authenticated
user-triggered formatting against real Rust Analyzer, a rendered VS Code
Extension Development Host format/save cycle through the provider-profile
backend, and full extension/browser suite results. This does not establish
acceptance of the packaged extension; formatter discovery and project repair
loops remain open. See
[language-server integration](native-language-server.md) and
[formatting evidence](evidence/native-language-server-formatting-local-20261011.json).

### Multi-folder language-server routing checkpoint (2026-10-11)

The native LSP HTTP query accepts an optional registered `folder_id`, routes to
a separately cached server rooted in that member folder, echoes the selected
identity and retires cached servers when membership no longer includes them.
The VS Code editor maps documents to their registered member root for
diagnostics, navigation and formatting, and reloads membership after folder
attachment. A real Rust Analyzer contract proved identical `src/lib.rs` paths
resolve to different primary/secondary symbols and reject unregistered or
removed IDs. AgentRunner's model-visible tool now accepts a `folder_id` only
from the workspace membership captured for that run; its focused native
contract verified secondary symbol dispatch and rejection before dispatch for
an unadmitted ID. The native language-server, multi-folder and AgentRunner
contracts passed **4/4**. The VS Code suite passed **307/307**. A full
provider-driven model tool turn and a live installed VS Code multi-folder
session are not yet verified. See
[multi-folder LSP evidence](evidence/native-multifolder-lsp-local-20261011.json).

## OpenCode V2 functional baseline

The source/API pin is v2.0.26. The following broader behavior baseline is drawn
from the official [V2 CLI](https://opencode.ai/v2/docs/cli/),
[intro and client modes](https://opencode.ai/v2/docs/),
[configuration](https://opencode.ai/v2/docs/config/),
[agents](https://opencode.ai/v2/docs/agents/),
[models](https://opencode.ai/v2/docs/models/),
[providers](https://opencode.ai/v2/docs/providers/),
[tools](https://opencode.ai/v2/docs/tools/),
[commands](https://opencode.ai/v2/docs/commands/),
[skills](https://opencode.ai/v2/docs/skills/),
[MCP](https://opencode.ai/v2/docs/mcp-servers/),
[permissions](https://opencode.ai/v2/docs/permissions/) and
[snapshots](https://opencode.ai/v2/docs/snapshots/),
[attachments](https://opencode.ai/v2/docs/attachments/),
[plugins](https://opencode.ai/v2/docs/plugins/),
[references](https://opencode.ai/v2/docs/references/) and
[sharing](https://opencode.ai/v2/docs/sharing/) documentation, checked on
2026-10-11. The V2 shell contract specifically defines background execution,
session completion notification and managed full-output retention; those are
separate acceptance items from xMind's existing foreground process and PTY
contracts. See [the native process implementation contract](native-process-tools.md#background-command-parity-contract).
The local coverage column describes the OSS target and current
evidence; it does not count Nexus-only or unverified behavior as parity.

| Area | OpenCode V2 behavior to match | xMind OSS evidence and remaining gap |
| --- | --- | --- |
| Runtime and clients | Full-screen and minimal interactive CLI, automation command, shared background service, explicit standalone/remote connection, web and desktop clients | One persistent native `xmind` backend, console CLI, browser UI and VS Code sidebar are in scope. Full interactive coding UX, consistent installed-client behavior and endpoint-by-endpoint compatibility remain incomplete; standalone Electron is explicitly Nexus-only. |
| Agents and orchestration | Build/Plan primary agents; foreground/background subagents; Markdown and config agents; per-agent model, visibility, permissions, step limits and defaults | Native single-agent and graph execution, YAML/Markdown authoring and named tool bindings exist. Exact primary/subagent modes, dynamic delegation, graph planning, configuration precedence and rendered selection remain incomplete. |
| Coding harness | Workspace read/edit, shell, search, LSP, custom tools, skills, attachments/references and review/undo workflows | Native file/search tools and approved edits have scoped contracts. A live browser run on the local xMind backend used the saved DeepSeek profile, proposed one exact `create_file` operation, waited for UI approval and completed the write with provider token metrics; its ignored smoke file was byte-checked and removed. This verifies one browser write path on runtime revision `3681c2e` (an ancestor of current source, not byte-identical to the current Release binary); see [live evidence](evidence/live-deepseek-browser-create-file-20261011.json). The current Release native server/CLI also completed one real `deepseek-flash` process-tool run using the backend's saved key, exact CLI approval and an isolated PowerShell profile; see [sanitized live evidence](evidence/native-live-cli-process-acceptance-20261011.json). `run_process(background=true)` plus `wait_process` has focused real-child/native HTTP/CLI tests for exact approval, streamed bounded events, complete paginated SQLite output artifacts (64 MiB cap), actual result handoff and restart replay. A fresh Windows backend now seeds the built-in trusted PowerShell profile, so `run_process` is available without a separate shell YAML file or per-command approval prompts. The shared browser/VS Code renderer offers read-only paged access to full stdout/stderr for observed completed commands; rendered browser/VS Code process acceptance remains open. Shell discovery/semantics parity, VS Code live write acceptance, broad attachments, complete diff/recovery and full-project coding workflows remain incomplete. The persistent native Content-Length LSP bridge passes real rust-analyzer symbol/diagnostics/navigation/formatting plus SQLite contracts; VS Code provides diagnostics, definition/hover/reference and formatting, including a rendered Development Host format/save cycle. See [native language-server integration](native-language-server.md) and [process tooling](native-process-tools.md). |
| Models and providers | Provider authentication, broad provider/model catalogue, per-session model selection, variants and provider/model request settings | Encrypted multi-provider profiles, native discovery and several native wire protocols exist. Saved credentials stay in the backend; clients receive public profile/model metadata only. When opaque history cannot cross provider wires, the backend preserves the conversation and the shared browser/VS Code failure card now explains that the request stopped before contacting the provider and that the saved key was not the cause. Cross-wire history conversion, variants, general settings precedence and broad live-client acceptance remain incomplete. See [provider wire recovery evidence](evidence/provider-wire-recovery-local-20261011.json). |
| MCP and extensions | stdio/HTTP MCP configuration and authentication; tools and integrations; plugins and custom commands | Native stdio/HTTP tools, resource reads and prompt retrieval use durable approval; modern/legacy stdio peers and official v2 HTTP SDK resource/prompt flows have scoped contracts. MCP `2026-07-28` list-change subscriptions are validated on both transports; AgentRunner fixtures and direct graph MCP fixtures over stdio and HTTP verify bounded sanitized events and listener shutdown before terminal state. Crash recovery/reconnect, OAuth renewal, other HTTP/SSE behavior, plugin lifecycle and complete command parity remain incomplete. AgentRunner refreshes a changed catalogue within the active run; a direct graph MCP node fails closed because its alias is pinned. |
| Permissions and change safety | Ordered allow/ask/deny rules over tools/resources, session permissions, change review, undo/redo and sharing controls | Backend-owned approvals and effect receipts are implemented for scoped operations. General policy rules, durable change snapshots/reversal and full export/sharing contracts remain incomplete. |
| Version control | Initialize Git, inspect status/branches and compare working-tree or branch diffs for coding/review flows | Native Git init/status/info/local branch list and working/branch/committed diffs operate on the opened workspace; browser and VS Code patch viewers, actual backend HTTP and CLI contracts pass. Rendered/installed acceptance, side-by-side source comparison, rename/binary cases, other providers and full VCS parity remain open. |
| HTTP and automation API | Versioned session, event, filesystem, agent, model, provider, tool and server APIs in the pinned OpenAPI schema | 141 operations are inventoried; 32 have reviewed native source candidates, 109 are unmapped, and none has verified operation-level parity. Native full/before-message session forking has HTTP, CLI, browser and VS Code contracts; broad endpoint parity remains incomplete. See [the generated inventory](OPENCODE_API_INVENTORY.json). |

This is a feature-level checklist, not an assertion that the cited docs are a
complete specification of every v2.0.26 implementation detail. For API work,
the tag's committed OpenAPI schema is the exact contract; for client behavior,
acceptance must cite the corresponding source behavior and a real native/client
test before a row can be called complete.
Refresh with `node Tools/audit-opencode.mjs`; the generated
[inventory](OPENCODE_API_INVENTORY.json) records the exact request, response,
schema and operation-specific acceptance criteria.

## Current native verification scope

The storage source `57da085` has passed all **118 hosted native** contracts in
**360.18 seconds**, **257 extension / 55 browser** checks and packaging. The
downloaded exact inventory, artifact digests, runtime/VSIX bytes, trusted synthetic
OAuth output, durable xlang3 SQLite storage and owned certificate cleanup verify
independently. This source precedes the new manual renewal coordinator and client
controls; it does not prove their integrated trusted exchange.
[Exact hosted storage evidence](evidence/native-oauth-refresh-storage-hosted-57da085.json).

The subsequent [manual renewal integration](native-mcp-renewal.md) connects the
native service, authenticated HTTP commands, unified console and Settings
controls. Its complete candidate gate is recorded separately below when verified.
Automatic expiry renewal, mid-run coordination and explicit uncertain grant
recovery remain required.

The repaired `0eee494` source passes all **117 hosted native** contracts in
**344.09 seconds**, **257 extension / 55 browser** checks and packaging. Downloaded
evidence confirms actual trusted native login/registered callbacks, encrypted
grant reopen, seven separate native refresh protocol modes and owned machine-root
cleanup. Digests, exact inventory and package bytes are verified. Synthetic
authority/tokens do not establish real-account, automatic/durable renewal or
installed/rendered acceptance; this source precedes the newer storage candidate.
[Exact hosted evidence](evidence/native-oauth-refresh-hosted-0eee494.json).

The newer durable refresh storage candidate passes the complete rebuilt **117/117**
local native gate in **389.30 seconds**, with all **2464** inputs unchanged and
three focused contracts passing. Actual xlang3 SQLite verifies exclusive claims,
credential/quiescence fences, atomic ciphertext/receipt rollback/publication and
generation restart retirement. Client source is unchanged; those suites were not
rerun. Current hosted **118-contract** acceptance, network coordination, automatic
renewal and client recovery remain unfinished. Both original failures are retained.
[Exact local evidence](evidence/native-oauth-refresh-storage-local.json).
[Storage ownership and remaining integration](native-mcp-refresh-storage.md).

The native checkpoint `6afaaf7` passes the complete **116/116**
local native gate in **382.06 seconds**, with all **2462** frozen inputs unchanged.
The original managed-profile contract passes without changing its watchdog.
Client inputs are unchanged from the prior complete **257 extension / 55 browser**
result; those suites were not rerun. Durable/automatic refresh, trusted native
login and current installed acceptance remain unfinished.
[Exact local evidence](evidence/native-oauth-refresh-local.json).

The registered-callback hosted gate at `6708269` passed **115 of 116** native
contracts in **407.85 seconds**. Trusted OAuth stopped before native login when
test-certificate import into `CurrentUser\Root` rejected noninteractive use.
Downloaded artifact digest, exact inventory and original logs are verified.
Trusted acceptance remains unproved. A subsequent test-only provisioning repair
uses the elevated isolated runner's LocalMachine Root store with exact owned-CA
checks and removal. Its actual hosted execution remains pending.
[Original hosted failure](evidence/native-oauth-registered-hosted-6708269.json).

The single-use refresh exchange primitive compiles and passes two focused native
contracts. Form encoding, rotation/retention, scope/metadata rejection, cancellation
and deadline retirement are checked, with actual untrusted TLS contact and no
HTTP dispatch. The first complete 116-contract local gate passed 115 and failed
managed profile startup at its unchanged 60-second fixture deadline. The original
output is preserved. The original focused repeat also failed. A longer-watchdog
diagnostic completed with a 57,196 ms cold start; that modified diagnostic is not
acceptance. Directory preparation now prepares and pins each destination once.
Its rebuilt original focused contract passes in 75.40 seconds with the unchanged
per-command watchdog; the following complete gate passes as recorded above. Hosted inventory now
requires 117. This primitive does not implement durable or automatic refresh,
running-owner coordination or client refresh controls.
[Implementation boundary](native-mcp-refresh.md).
[Original full-gate failure](evidence/native-oauth-refresh-first-failed.json).

The preceding `73ccf90` console package passed **114 hosted native** contracts in
**417.25 seconds**, **257 extension / 55 browser** checks and independent artifact,
source/runtime/VSIX and twelve browser asset comparisons. It predates callback
registration and refresh work and has no current installed/trusted-login claim.
[Exact hosted evidence](evidence/native-oauth-cli-hosted-73ccf90.json).

The registered-callback candidate adds native callback path/port configuration
and exact exclusive binding, with purpose/agent authority changes on registration
changes. The complete rebuilt **115/115** native gate passes in **388.83 seconds**,
with **2460** unchanged mapped inputs. Focused checks also pass **four** selected
contracts. Client inputs are unchanged from the prior complete **257/55** result;
client suites were not rerun here. Hosted trusted-login acceptance remains pending.
[Registered callbacks](native-mcp-registered-callback.md).
[Exact local evidence](evidence/native-oauth-registered-local.json).

The trusted OAuth candidate passes the complete **115/115** local native gate
in **386.74 seconds**, plus **257 extension / 55 browser** checks, with **2459**
unchanged inputs. The additional local test checks the independent synthetic
HTTPS authority; native trusted login is compiled but not executed locally.
[Exact local evidence](evidence/native-oauth-trusted-local.json).

An actual-service trusted HTTPS OAuth fixture is compiled and prepared for the
isolated Windows runner. Its independent authority checks native S256/code
exchange and negative callbacks; the native fixture requires encrypted grant
publication/reopen and actual MCP bearer use. The hosted exact inventory now
requires **116** contracts. Local peer and execution-boundary checks do not
establish successful native login; that hosted result remains unverified.
[Acceptance boundary](native-mcp-trusted-acceptance.md).

The corrected complete hosted view gate at `73ccf90` passed **257 extension /
55 browser** checks. Downloaded artifact, exact source and regenerated asset
hashes were independently verified; this does not establish native or current
installed/rendered acceptance.
[Hosted evidence](evidence/native-views-hosted-73ccf90.json).

Native MCP console setup/status/watch/cancel commands now use the same backend
service as graphical Settings and pass **114/114** local native checks in
**388.18 seconds**, with **2455** unchanged inputs. The complete **257 extension /
55 browser** suites also pass. Actual native-service integration covers encrypted
grant metadata and untrusted TLS rejection; independent synthetic HTTP fixtures
cover CLI ownership, CAS, detach and private-field/link rejection. Trusted HTTPS
positive login, real browser launch, registration, refresh/revocation and current
installed/rendered acceptance remain unfinished. The prior hosted view manifest
failure is retained; its corrected full inventory passed the hosted view gate above.
[Console implementation](native-mcp-console.md),
[exact local evidence](evidence/native-oauth-cli-local.json),
[hosted failure](evidence/native-mcp-login-view-ci-f595205.json).

Authenticated MCP setup routes, the asynchronous C++ login owner and browser/VS
Code Settings controls pass **113/113** local native, **257 extension / 55 browser**
checks with **2454** unchanged inputs. Actual native service/SQLite and browser
adapter/controller acceptance covers TLS-negative login, status, request identity,
CAS, cancellation and no untrusted-server HTTP dispatch. Client/DOM/host fixtures
verify public metadata ownership, HTTPS links and reload/detach without replay.
This does not prove trusted HTTPS positive OAuth login, registration selection,
refresh/revocation, CLI login, complete connector configuration or current
installed/rendered acceptance. [Implementation boundary](native-mcp-login.md),
[exact evidence](evidence/native-oauth-service-local.json).

OAuth grant persistence/factory integration passes **112/112** local native
contracts in **384.25 seconds**, with **2446** unchanged inputs. Actual xlang3
SQLite verifies encrypted complete grants, atomic snapshots/rotation/rollback,
reopen and purpose/context binding. Native authority includes grant revisions;
an independent socket peer verifies retirement before any request after known
expiry. This is no authenticated setup API, trusted HTTPS login, automatic
refresh/revocation or current installed/view acceptance. The earlier client
suites apply to unchanged client source and were not rerun here.
[Exact component evidence](evidence/native-oauth-grants-local.json).

Authorization-owner source `dd08604` has separately passed **110 hosted native**
contracts in **332.01 seconds**, **244 extension / 54 browser** suites and
downloaded bundle/VSIX verification. It predates callback/grant-storage work.
No trusted HTTPS login or current installed/rendered acceptance follows from
this package. [Exact hosted evidence](evidence/native-oauth-authorization-hosted-dd08604.json).

The native loopback callback library component passes the **111/111** local
gate in **378.16 seconds**. A subsequent test-only callback rebuild passes all
**12** real-socket cases in **0.99 seconds**, including port closure while the
native process/receiver remain alive. Production inputs/binaries did not change;
the complete gate was not repeated for the two strengthened fixtures. This is
no authenticated setup API, encrypted OAuth grant persistence, trusted HTTPS
login or client login flow. [Exact evidence](evidence/native-oauth-callback-local.json).

The configured HTTP/terminal-feed source `7007010` passed the complete hosted
**110 native / 244 extension / 54 browser** gates and packaging. Downloaded
artifacts are independently verified by raw hashes, exact test inventory and
bundle/VSIX bytes. This package predates the newer OAuth authorization owner and
callback listener and has no current installed/rendered acceptance.
[Hosted verification](evidence/native-mcp-http-hosted-7007010.json).

New native OAuth authorization components pass **110/110** local contracts in
**376.23 seconds**, with **2439** unchanged mapped inputs. Independent socket
checks validate the production code-grant form and RFC PKCE; native fixtures
validate callback binding/expiry/replay/denial, private token parsing and TLS
rejection. This does not deliver trusted HTTPS login, callback listener/routes,
registration selection, encrypted OAuth persistence, refresh/revocation or view
login controls. [Component evidence](evidence/native-oauth-authorization-local.json).

Native MCP HTTP remains partial. The native owner, decoder, metadata projection,
shared result validation and configured factory have compiled. The actual
official HTTP SDK effect contract passes approved JSON/SSE writes, denial,
duplicate rejection, lost-reply uncertainty and xlang3 SQLite restart. Native
configuration/authority assertions and OAuth metadata/challenge/401/TLS-negative
fixtures pass. The complete revised local gate passes **110/110** in **371.74
seconds**, with **2438** unchanged inputs and full **244/54** synthetic client
suites. [Exact component scope and raw evidence](evidence/native-mcp-oauth-local.json).
Hosted packaging and installed public agent/graph workflows remain separate
verification requirements. Positive OAuth
discovery/login/token exchange, broader protocol support and older HTTP lifecycle
remain unfinished.
[Scope and pinned protocols](native-mcp-http.md).

Native file discovery and literal content search now interpret hierarchical
workspace-local Git/search ignore metadata, ordered negation and scoped overrides.
Positive glob behavior matches the inspected adapter's file-whitelist/directory
pruning behavior on owned reference fixtures. All 98 native and 215/39 client
contracts passed; actual OpenAI filtered search and xlang3/SQLite restart passed.
Actual Git corpus decisions match the native assertions; two installed-ripgrep
dialect differences remain explicit. This does not establish global-ignore,
complete parser/regex/large-file search parity or installed writing acceptance.
[Scope and remaining work](native-ignore.md), [exact evidence](evidence/native-ignore-local.json).

Native `glob_files` provides recursive patterns, brace alternatives, character
classes and Unicode filename matching, with scoped retained-handle traversal,
hidden-file choice and explicit coverage bounds. All 98 native and 215/39 client
contracts passed; real OpenAI discovery and xlang3/SQLite history restart passed
separately. Graph and delegated discovery use the same native implementation.
Gitignore/global-ignore rules, escaped literals and full search parity remain
incomplete. [Behavior and limits](native-glob.md), [exact evidence](evidence/native-glob-local.json).

Native `read_file` now implements optional 1-based `offset` and bounded `limit`
arguments, streamed UTF-8 validation, explicit per-line clipping and continuation
offsets. Its complete 98-contract native and 215/39 client gates passed, followed
by an actual OpenAI read of a 4.29 MB file and xlang3/SQLite history restart.
Path-only complete reads retain their existing 1 MiB limit. This is partial
read-tool parity: directory paging, images/PDFs, omitted long-line suffix
recovery and full coding parity remain incomplete. Installed VS Code writing
still requires acceptance of a fresh installation of the current format;
no migration or migration approval is required. [Exact behavior and limits](native-file-pages.md),
[local evidence](evidence/native-file-pages-local.json).

Native workspace skill discovery, loading, context delivery and effect approval
bindings passed the complete 90-contract hosted native gate, browser/native
integration and packaging at source `d767d506`. Independent verification binds
the packaged runtime and view sources to that commit. That package is installed
and passed an actual VS Code opened-folder host plus four real-provider skill
activation/companion reads. Rendered acceptance and broader skill delivery
remain pending; this is not a verified equivalent
OpenCode skill operation. [Skill scope and remaining delivery](native-skills.md).
The shared skill approval display passed the complete isolated 176-extension /
35-browser view gate. It displays source bindings for file, command and MCP
approvals; full skill delivery and rendered acceptance remain
separate requirements. [Verified view evidence](evidence/native-skill-approval-views-hosted.json).
The [separate installed/live receipt](evidence/native-skills-installed-live.json)
retains the initial Claude formatting and harness cleanup failures and the
successful Claude retry. The browser preview subsequently upgraded to the same
accepted runtime with exact saved-record comparison. Reload and real skill
metadata rendering passed, but a moving approval-card interaction failed and
was cancelled before an effect. The DOM-retention fix subsequently passed its
complete isolated 177/35 gate and is published in the browser preview. A real
retry passed expanded-guidance retention, sidebar approval, native file creation,
exact final response and reload with actual metrics. The fixed package at
`5d48bdd72713dcc23d5a1b15a0a08d3e3b7b1fd2` subsequently passed its complete
90-contract native workflow and replaced the normal VS Code profile's installed
package. Its full runtime and view inventory was verified with unchanged
settings. After the user granted workspace trust, the installed package passed
a real OpenAI `read_file` run in the right sidebar for `TestProj`. A VS Code
window reload restored the same completed run, response, model, workspace root
and actual token metrics without re-entering keys. This persistent-profile window
loads a separate development workspace-opener helper; xMind is the installed
package. This verifies one workspace-read UI path; rendered skills and full
skills remain incomplete.
[Trusted workspace acceptance](evidence/native-vscode-trusted-workspace.json).
[Package and installation evidence](evidence/native-approval-stability-hosted-package.json).
The subsequent schema13 source passed the exact full 91-contract local native
gate and real OpenAI continued-session restoration after backend exit/restart,
including reading a changed guide's new companion without reloading the skill.
Selections commit atomically with native tool turns and remain bound to the
session/workspace or child run. Initial constructor-check and live harness
configuration failures are retained in
[local persistence evidence](evidence/native-skill-state-local.json).
Hosted packaging and installed/rendered schema13 acceptance remain pending.
Native catalogue inspection subsequently passed the exact 92-contract local
gate and complete 178/36 view suites. It is available through the authenticated
backend, native CLI and shared client, including scoped browser-cookie routing.
The earlier source's manual attachment/removal and rendered catalogue controls
were incomplete. Newer native user controls and inline view controls passed
the full 92 native / 184 extension / 38 browser local gates. Three real OpenAI
runs passed user attachment of model-disabled guidance, removal, persisted
reattachment and fresh source/companion restoration after backend restart.
Hosted packaging and installed/rendered acceptance remain separate.
[User-control evidence](evidence/native-user-skill-controls-local.json).
Its complete isolated hosted 184/38 view gate also passed. Independent archive,
source and asset verification binds every tracked view input to `c42a5c4`.
[Hosted view verification](evidence/native-user-skill-controls-views-hosted.json).
That exact `c42a5c4` source also passed its complete hosted native workflow and
packaging. Independent archive/source verification checks all native and packaged
view files. It is not installed in the running previews.
[Verified package](evidence/native-user-skill-controls-hosted-package.json).
Newer CLI attachment/removal/clear controls passed all 92 native contracts locally
in 175.20 seconds, including actual Unicode arguments and stale/concurrent
selection rejection. Synthetic fixtures admit no inference runs; hosted and
installed CLI acceptance remain separate.
[CLI evidence](evidence/native-cli-user-skills-local.json).
The managed launcher still needs a native generation handoff preserving the same
profile database before existing-profile upgrade acceptance can be claimed.
[Upgrade requirements](native-runtime-handoff.md).
Newer owner-retention source passed complete local 189/38 view suites. It keeps
the authenticated existing backend across package/settings changes and fails
explicitly on an unknown owner instead of creating an empty replacement profile.
An actual model-free native API comparison retained saved records and one process;
its cleanup-receipt defect and benchmark-deferred correction are recorded.
This is not native runtime migration or installed/rendered upgrade acceptance.
[Retention scope](evidence/native-workspace-owner-continuity-local.json).
The ordinary VS Code window was initially observed in Restricted Mode, which
disabled xMind. That historical boundary remains recorded; the user subsequently
granted trust and the workspace-read UI acceptance above passed.
[Catalogue and window-boundary evidence](evidence/native-skill-catalogue-local.json).
The exact `7b1dc1d` source subsequently passed its complete hosted 92-contract
native workflow and packaging. Independent verification checks the archives,
native inventory and packaged host/view bytes against that commit. This file-only
check does not establish installed schema13 acceptance or include newer user
attachment controls.
[Hosted package verification](evidence/native-skill-catalogue-hosted-package.json).
[Browser evidence](evidence/native-skills-browser-upgrade.json).
[Verified view gate](evidence/native-approval-stability-views-hosted.json),
[actual approval retry](evidence/native-approval-stability-live.json).

The later catalogue-recovery source passed its isolated hosted **175/35** view
gate and its tested browser asset is published. Rendered recovery and an updated
VSIX remain pending; the local benchmark guard has deferred further local
validation. This fixes an adapter recovery path without claiming a new native
gate or broader behavioral parity.
[Catalogue recovery](model-catalogue-recovery.md).

The earlier browser preview source `60475f84` passed its full 89-contract gate and real
four-provider native workspace reads. Its installed browser upgrade preserved
existing API records and passed actual four-provider rendered runs and refresh.
The later compact-height CSS passed 173/33 frontend tests and actual short-pane
rendering. These checkpoints do not establish full parity: rendered normal
VS Code interaction, concurrent provider/session catalogue recovery, live
compaction and broader provider/coding capabilities remain separate work.
[Model policy](native-model-eligibility.md),
[browser upgrade](browser-preview-upgrade.md),
[compact sidebar](compact-sidebar.md).
The older acceptance paragraphs below retain their original checkpoint scope.

Current schema-v12 context, YAML configuration and DeepSeek source configured
and compiled in the second complete **88-contract** attempt, which passed
**85 and failed three in 162.98 seconds**. The first 86-contract attempt passed
81 and failed five. Both failures are retained. After repository/controller and
CLI fixture repairs, the third full gate passed **88/88 in 161.48 seconds**,
with zero failures/skips, all 560 frozen inputs unchanged and original test
output captured. DeepSeek gateway/profile
contracts passed individually in **0.54 / 0.71 seconds** with synthetic replies.
The earlier **131 extension / 29 browser** source tests retain their scope.
The final saved-provider footer passed **141 extension / 32 browser** tests,
with all 37 frozen inputs unchanged. It loads catalogues using saved credentials
and keeps the provider and model controls together at the bottom of the sidebar.
[Frontend source evidence](evidence/native-provider-footer-ui-provenance.json)
does not establish installation or live-provider acceptance. The later
model-free native/browser integration passed in **3.30 seconds** and the
actual VSIX passed **18 asset checks**, with all 12 tested view files matching.
[Integration/package evidence](evidence/native-provider-footer-integration-provenance.json)
has its own scope and retains the initial guard-host failure.
The complete local pass establishes its contract scope. A later Responses
serializer defect was repaired and passed the fourth complete **88/88 gate in
171.89 seconds**, with all 560 source inputs unchanged and no failures/skips.
[Exact local evidence](evidence/native-context-provider-local-provenance.json)
binds that repair and the original output;
installed/live context and broader provider strategy acceptance remain separate.
[Current implementation](native-context-compaction-design.md),
[provider scopes](provider-setup.md).

All four provider accounts passed separate direct HTTPS catalogue and small
generation checks. The installed ace/schema-v10 preview still has one configured
OpenAI Responses profile and four advertised routes, with DeepSeek absent; the
local four-key configuration has not been imported. An earlier run returned HTTP
400 `invalid_value`; an isolated direct reproduction later identified legacy
assistant `input_text` instead of `output_text` in both ace and current source.
A fresh native OpenAI browser prompt separately returned `OK` with supplied
usage **3884 / 5**. That success does not validate legacy-history replay or
four-provider native coding; direct account smoke checks remain separate.

The native [JSON POST transport foundation](native-context-transport.md)
passed its complete local **78-contract gate in 193.91 seconds**, with 427
frozen inputs and the accepted xlang3 runtime verified unchanged. It supplies
bounded transport; the later compaction and token-counting implementation has
the separate local scope above. Exact `859aca7` subsequently passed its separate
hosted **78 native / 119 extension / 26 browser** gate, all 16 steps, model-free
integration and 18 VSIX assets. That transport checkpoint contains no compaction
acceptance. [Local evidence](evidence/native-context-transport-local-provenance.json),
[hosted transport evidence](evidence/native-context-transport-hosted-859-provenance.json).

Earlier exact source `2cd392f` passed the [hosted MCP gate](https://github.com/xlang-foundation/xMind/actions/runs/37824215372):
**78 native / 119 extension / 26 browser** tests, all 16 steps, model-free
integration and 18 VSIX assets. Exact Git, raw logs, archive digests and
payload maps were verified; the installed preview remains ace/schema v10.
[Hosted evidence](evidence/native-dynamic-mcp-hosted-2cd-provenance.json).

The bounded dependency-planning implementation now has a complete local
**78-contract gate** covering actual MCP effects inside dynamically admitted
coding children. Five new integration scenarios verify exact child-owned
approval and captured protocol bindings, a real peer write with a separate
native verification read, denial/cancellation before dispatch, and lost-reply
uncertainty without reopening replay. The provider replies and bespoke legacy
peer descriptors are synthetic; native agents, effects and xlang3 SQLite are
real. The initial malformed fixture failure remains separate. Frontend source
and the installed ace/schema-v10 preview are unchanged. This does not establish
live planning, editor acceptance, recursive planning, deterministic dynamic
nodes, skills, compaction, outbound A2A or complete coding/provider parity.
[Local78 evidence](evidence/native-dynamic-mcp-local-provenance.json),
[planning scope](native-dynamic-plan.md).

Native Responses reasoning continuation passed the exact ace2460 hosted gate: **71 native contracts in 171.52 seconds, 103 extension and 20 browser contracts**, with all 16 job steps, native/browser integration and 18 VSIX assets passing. Verified sources and artifact maps bind the completed-item continuation fix and its real native two-child read/join with synthetic provider replies. The local build was deferred by the benchmark guard. The exact native bundle is installed. One real OpenAI browser prompt completed two read-only native children and a joined parent response, with six owned model attempts, separate per-response metrics and zero effect operations. Immediate refresh retained the connection and selection. Upgrade and rollback checks passed at their separately documented scopes. [Compatibility and scope](native-responses-reasoning.md), [exact hosted evidence](evidence/native-responses-reasoning-hosted-provenance.json).

Ordinary Agent mode now offers native `delegate_tasks` for model-selected read-only workspace investigations. Actual child AgentRunners keep separate conversations and measured response usage/timing, share their parent's call/deadline limits, and return observed results before the parent continues or requests an approval-controlled effect. Schema v10 durably binds tasks, immutable presets, budgets and once-only settlements; recovery never replays children. CLI, browser and VS Code observe the same owned child histories and committed tree cursors.

For the preceding delegation checkpoint, the final guarded local **71-contract native gate passed in 142.36 seconds**, with an exact expected/registered/passed manifest, **45 frozen source hashes**, zero failures/skips and no exclusions. The delegation engine contract took **1.96 seconds**, actual HTTP/CLI/shared-controller acceptance **1.03 seconds**. **103 extension tests** and **19 browser tests**, fresh source-matched browser/native integration and 18-asset VSIX verification also passed. Provider replies, signatures and keys are explicitly synthetic; native agents, filesystem effects, permissions, xlang3 SQLite and transport are real. The [initial process-fixture positional-schema failure](evidence/native-delegation-initial-ctest.log) is retained separately; the corrected frozen source passed all 71 contracts.

Exact revision `e353a37799530a234a6fa13e51f61a5c52d3ae6a` subsequently passed its [hosted gate](https://github.com/xlang-foundation/xMind/actions/runs/37765666399): **71 native contracts in 175.51 seconds**, **103 extension tests in 2.9668103 seconds** and **19 browser tests in 1.426115 seconds**, plus native/browser integration and verification of all 18 required VSIX assets. The delegation engine took **4.88 seconds** and its HTTP/CLI contract **1.22 seconds**. All 16 job steps succeeded, with zero failures/skips and the exact complete native manifest. Downloaded artifact digests, exact source and runtime/stdlib pins were verified. [Exact hosted71 provenance](evidence/native-delegation-hosted-provenance.json), [raw hosted CTest](evidence/native-delegation-hosted-ctest.log), [original hosted job](evidence/native-delegation-passing-ci-job.log).

The preceding managed backend upgrade installed the exact hosted e353 bundle with schema v10; the later 7fe upgrade is recorded in the diagnostic scope. The actual upgrade preserved existing records, settings and browser access; its disposable schema 9→10→9 rollback fixture passed separately. The original e353 webpage exposed a classic-script name collision. A separate local browser repair passed **103 extension tests in 1.6557025 seconds**, **20 browser tests in 0.9090882 seconds**, native/browser integration and all 18 VSIX assets. The repaired page reused its cookie without key entry and restored prior history/metrics, the selected model and Agent mode. These frontend results are separate from the unchanged hosted 71/103/19 gate. [Upgrade evidence](evidence/native-delegation-upgrade-provenance.json), [acceptance scopes](native-delegation-acceptance.md).

The first e353 live `delegate_tasks` request failed with `responses_terminal_mismatch` before child admission: zero children, only the user history row and one parent model-budget attempt. That historical failure remains recorded; the later ace browser two-child join passed the separate acceptance above. Installed VS Code delegation acceptance remains unverified. Mutable dependency planning, skills, compaction, outbound A2A and full provider/coding parity remain incomplete. [Implementation and limits](native-delegation.md), [next native planning design](native-dynamic-plan-design.md).

New source adds direct MCP graph tool nodes and explicit offline native alias
discovery. Nodes bind an opaque alias to trusted server ID/revision, preserve
raw literal arguments inside a string, and rediscover that exact alias before
using the existing durable approval/effect journal. Graphs can run without a
model; metadata/admission do not start peers. Changed paused bindings remain
inspectable/cancellable and cannot resume against another revision. Oversized
acknowledged graph output fails the child while preserving the succeeded
operation. The final guarded **69 native contracts passed in 121.22 seconds**,
with the exact expected/registered/passed manifest, zero failures/skips and no
post-build exclusions. The graph MCP contract took **5.59 seconds** and actual
HTTP/admin/CLI/view-adapter contract **2.31 seconds**. They verify real approved
external writes/dependent reads, raw arguments and xlang3 SQLite reopen,
denial/cancellation, descriptor drift, stale pauses, acknowledged output bounds,
reply-loss quarantine and journal fail-stop without replay. Fourteen frozen
source hashes match the pre-build capture. Fresh browser/native integration
passed; unchanged frontend sources retain exact fceb hosted **98 extension and
17 browser tests**, without rerunning those suites for this native change.
Exact source `c4ec09fc3ee25c1b3a2bd9087b25f5af420ee616` subsequently passed
hosted **69 native contracts in 184.36 seconds**, **98 extension and 17 browser
tests**, native/browser integration and 18-asset VSIX verification, with zero
failures/skips and all 16 job steps successful. Live direct-MCP graph and
rendered IDE acceptance remain pending; provider/parity counts are unchanged.
The initial HTTP start-guard failure remains separate historical evidence.
[Local69 evidence](evidence/native-graph-mcp-local-provenance.json),
[exact hosted69 evidence](evidence/native-graph-mcp-hosted-provenance.json),
[Direct MCP graph scope](native-graph-mcp.md).

The [native delegation design](native-delegation-design.md) now has a bounded
implementation and exact local/hosted71 acceptance above. It does not establish
runtime replanning, live multi-agent acceptance or verified OpenCode parity.

The ordered-Claude-history local checkpoint passed **67 native contracts in 117.65 seconds**,
with an exact manifest, zero failures/skips and no post-build exclusions. Strict
ordered Claude receipts preserve accepted raw tool inputs, escaped keys and
signed/interleaved blocks through actual AgentRunner file execution and xlang3
SQLite replay. The helper contract took **0.50 seconds** and expanded agent
contract **0.57 seconds**; unsigned-block rejection, receipt/DTO mismatch and
aggregate bounds also passed. Signatures/redacted data remain opaque, without
local cryptographic verification.

Fresh browser/native integration passed against the rebuilt server. Unchanged
frontend sources match `ae7c4ba` and retain its recorded **98 extension and 17
browser tests**, without rerunning those suites for that local native change.
Exact source `fceb50ba0493f645ee5f0a00d5400e0f103df231` passed its hosted
**67 native contracts in 120.18 seconds**, **98 extension and 17 browser tests**,
native/browser integration and VSIX verification, with zero failures/skips and
all 16 job steps successful. Its history helper took **0.31 seconds** and
actual Claude agent contract **1.03 seconds**. That hosted67 result excludes
newer direct-MCP-graph source and its separate local 69-contract gate.
[Exact hosted67 evidence](evidence/native-anthropic-history-hosted-provenance.json).
Thinking request controls, live models, rendered
IDE acceptance, account/model binding and foreign signed-history conversion
remain incomplete. [Local67 evidence](evidence/native-anthropic-history-local-provenance.json),
[Claude history scope](native-claude-history.md).

The preceding local Claude checkpoint passed **66 native contracts in 114.49 seconds**,
**98 extension and 17 browser tests**, with zero failures/skips, an exact native
manifest and no post-build exclusions. Actual Claude AgentRunner file reads,
matching tool results, xlang3 SQLite history/reopen, cancellation/recovery and
second-tool-row SQL rollback passed with synthetic provider replies. Native
usage events and persisted responses retain supplied uncached input/output/cache
counts and zeros without sums or invented totals; actual DOM fixtures verify
their live/history labels. Fresh browser/native integration passed against the
rebuilt server. The new agent contract took **0.56 seconds** and adapter contract
**0.23 seconds**. Exact source
`ae7c4ba07d59f57cbe393eff5a01b7d5186dfbc5` also passed its hosted **66 native
contracts in 131.92 seconds**, **98 extension and 17 browser tests**,
native/browser integration and VSIX verification, with zero failures/skips. Its
Claude agent contract took **1.41 seconds**.
[Exact hosted66 evidence](evidence/native-anthropic-agent-hosted-provenance.json).
[Local66 evidence](evidence/native-anthropic-agent-local-provenance.json),
[Claude scope](native-claude-agent.md).

That66 result excludes the newer ordered thinking/signature receipt source above.
It does not establish live inference, rendered Claude IDE acceptance or
acceptance of the 21 pinned Anthropic models. Provider
and OpenCode parity counts are unchanged; installed previews are unchanged.

The cleanup source `dea588874e5a8c8e40bf1a158fa925520443c8a0` passed its hosted
gate: **58 native, 88 extension and 17 browser contracts**, native/browser
integration and VSIX verification, with zero failures or skips.
[Exact cleanup evidence](evidence/native-cleanup-hosted-provenance.json).

The later source `ddd1d3da087f9ca7f8b0b8d81705c82632e15094` passed its separate
hosted gate: **60 native, 88 extension and 17 browser contracts**, native/browser
integration and VSIX verification, with zero failures or skips. This adds native
Gemini request/SSE/HTTP transport and signed replay data, using labelled
synthetic socket fixtures. It excludes the later common gateway/history bridge.
[Exact transport evidence](evidence/native-gemini-transport-hosted-provenance.json).

The gateway/history source `75f45f0a036f1ffbab8c4b157df364f3697b52a0` passed
its exact hosted gate: **61 native, 91 extension and 17 browser contracts**,
native/browser integration and VSIX verification, with zero failures/skips.
Its native gate took **121.84 seconds**. This includes common gateway/history
receipts, separate tool identities, supplied usage, strengthened callback
assertions and escaped large-response regressions. The earlier local 61 result's
post-build exclusions remain historical; this hosted gate executes those cases.
[Exact hosted evidence](evidence/native-gemini-history-hosted-provenance.json).

The subsequent agent source `c9591fe79cad9a4253ac8088933f0c8a2848ded1` passed
**62 native contracts locally in 100.44 seconds**, including actual native file
execution and signed replay after xlang3 SQLite reopen with synthetic provider
replies. Native/browser integration passed again against that compiled server;
its unchanged thin-client sources matched the earlier 91/17 pass.
[Local agent evidence](evidence/native-gemini-agent-local-provenance.json).
Its exact hosted gate also passed **62 native, 91 extension and 17 browser
contracts**, native/browser integration and VSIX verification, with zero
failures/skips. Its native gate took **128.80 seconds**.
[Exact hosted agent evidence](evidence/native-gemini-agent-hosted-provenance.json).
That hosted result excludes the new catalogue/enrollment source below.

Enrollment source `2f5e0f05e9bfadcf86b5508863da0ec5a9e78cfe` passed its
exact hosted **64 native, 94 extension and 17 browser contracts**, native/browser
integration and VSIX verification, with zero failures/skips. Its native gate
took **158.09 seconds**; the earlier local 64-contract gate took **120.98
seconds**. Native Gemini catalogue pagination, encrypted enrollment, separate
per-model tool policy, actual owned-key file execution/cancellation/CAS failure
and signed xlang3 SQLite replay passed with synthetic provider replies. The
direct-SQLite removal and shared `records.hpp` cleanup are included.
[Exact hosted enrollment scope](evidence/native-gemini-enrollment-hosted-provenance.json).

The preceding CLI source passed all **65 native contracts locally in 113.16 seconds**,
with its exact manifest, zero failures/skips and no post-build exclusions. The
new CLI contract took **4.23 seconds** and covers OpenAI/Claude/Gemini profile
catalogues, private environment setup, selection, all-family secret reflection
rejection, safe provider diagnostics, signed Gemini text/history/SQLite reopen,
stale ownership without retry and actual failed-turn/recovery status. Fresh
browser/native integration passed again; that checkpoint's unchanged frontend
sources retained the earlier verified **94 extension and 17 browser tests**,
without rerunning those suites for the CLI change.
[Local CLI scope](evidence/native-provider-profile-cli-local-provenance.json).
Exact CLI source `ac69c1f2a2c4a757b23d0d9da8b98d6188fab3a8` subsequently
passed its hosted **65 native contracts in 149.37 seconds**, **94 extension and
17 browser tests**, native/browser integration and VSIX verification, with zero
failures/skips. The CLI contract took **4.16 seconds**; the exact ac69 manifest,
pinned runtime/stdlib/patch and original logs were verified.
[Exact hosted CLI evidence](evidence/native-provider-profile-cli-hosted-provenance.json).
This hosted65 result excludes newer Claude66 agent/metrics and Claude67 history
source. GenerateContent
eligibility alone does not establish tool capability or live acceptance. Live
Gemini inference and actual Gemini IDE acceptance remain unverified. See
[provider setup](provider-setup.md), [current validation](VALIDATION_STATUS.md)
and [milestones](milestones.md).

An earlier installed browser preview used native `19d69dd` and view `6f32d215`;
its [historical scope](provider-setup.md) is preserved. The independently
accepted c4 installed preview has its own
[historical evidence](preview-checkpoint-c4ec09fc.md). The preceding exact e353
backend upgrade, separately repaired rendered browser and failed first live
delegation request are recorded in [current acceptance](native-delegation-acceptance.md).
They do not establish successful live delegation or installed VS Code acceptance.

Test counts describe verification scope, not a parity percentage. No OpenCode
operation is declared equivalent merely because a component contract passes.
Complete coding, provider, protocol, CLI and editor acceptance remains required.

## Pinned API inventory

The pinned source lives in the ignored `.agentflow/reference/opencode` checkout.
`Tools/audit-opencode.mjs` verifies its commit and reads the committed OpenAPI
schema, so local reference edits cannot silently change the baseline. It
generates [OPENCODE_API_INVENTORY.json](OPENCODE_API_INVENTORY.json): **141
operations**, with upstream parameters/body/responses and acceptance requirements.
Earlier upstream audit snapshots counted 139; the pinned schema count is
authoritative.

Removed-prototype mappings are absent. The native mapping identifies **31
partial C++ source candidates and 110 unmapped operations**. Each candidate
records a checked source anchor, SHA-256, surface and explicit gap. Unmapped
means no reviewed mapping, not proof that no related component exists. Source
presence and hashes do not establish behavioral equivalence or executed
CLI/editor acceptance. **Zero operations are claimed as verified parity.** The
API inventory does not cover every UI, plugin, model/provider or tool behavior.

## Native capability coverage and remaining acceptance

This table records current native components and delivery gaps. It replaces the
removed Python prototype's status table; it is not a completion score.

| Capability | Native component scope | Remaining acceptance |
| --- | --- | --- |
| Persistent sessions and run events | C++ repositories, ownership, transactions, replay and recovery use SQLite through embedded xlang3 | Complete concurrent-client conversations, failure/recovery and release acceptance |
| Agent/model/tool loop | Shared C++ single-agent/graph execution, hosted71 read-only delegation, hosted77 bounded dependency planning/revision and local78 coding-child MCP effects with durable budgets and signed continuation | Broad live coding tasks, live/rendered/installed planning acceptance, recursive/deterministic dynamic nodes, context bounds and production recovery |
| Models and providers | Native OpenAI Chat/Responses, Claude Messages, Gemini GenerateContent, DeepSeek Chat and xAI Responses routes have scoped transport/tool/history/profile contracts. The native xAI route passes synthetic Grok 4.7 catalogue discovery and an actual native AgentRunner workspace read; no live xAI request or complete model-family parity is established. The Responses legacy serializer repair passed its fourth complete gate. Separate actual four-provider skill/tool scenarios are recorded in [skill acceptance](native-skills.md) | Complete live coding and current installed-client acceptance, broad per-model authentication/capability coverage, cloud/local provider groups, thinking and media features; see [MODEL_SUPPORT.md](MODEL_SUPPORT.md) |
| File/search/edit/process tools | Native workspace reads/search, approval-backed edits/creation, fuzzy file/directory discovery and foreground plus bounded background process operations have scoped contracts. The native finder is exposed to single/graph agents and through a view-authorized HTTP route; its API and client gaps remain documented in [native fuzzy discovery](native-fuzzy-find.md). Windows ConPTY sessions now run inside the native server workspace with authenticated HTTP and console-client create/list/read/write/resize/close; the CLI maps Windows console keys to VT input and follows console resizes, while VS Code bridges its native terminal; see [terminal sessions](native-pty.md). Missing-parent creation now has native ancestor/guidance binding, disclosed folder effects and separate real OpenAI CLI create/edit/deny acceptance; see [parent creation](native-parent-creation.md) | Full-output process artifacts and reconciliation, full-screen application acceptance in CLI and VS Code, mouse input, terminal persistence across backend restart, installed/rendered approvals and complete repository coding tasks |
| Instructions and agent presets | Native repository guidance discovery, source binding, durable approval checks and explicit workspace.inspect revision1 leaf policy | Full instruction scoping/configuration and broader reproducible presets |
| Skills and commands | Native workspace discovery/loading, catalogue inspection, model activation, user attachments/removal and persisted selections have scoped native and actual OpenAI/other-provider acceptance; see [skills](native-skills.md) | Full command/skill lifecycle, source management, URL/embedded sources, broader scoping and current installed/rendered acceptance |
| Plugins | Native ownership is specified | Lifecycle, hooks, isolation and compatibility acceptance |
| MCP | Configured native stdio/HTTP tools, schema worker, approvals and modern/legacy official SDK peers have scoped contracts. Native resources/list-read and prompts/list-get use the same approval registry and durable journal; pinned official v2 SDK HTTP tests cover approved/denied retrieval and SQLite reopen. Both modern transports validate `2026-07-28` `subscriptions/listen`; AgentRunner and direct graph MCP child contracts verify bounded sanitized notifications and pre-terminal listener shutdown on stdio and HTTP, while HTTP AgentRunner also proves an approval-gated effect. See [subscription validation](VALIDATION_STATUS.md#mcp-2026-07-28-subscriptionslisten-for-agents-and-graphs--2026-10-12). The modern HTTP owner handles correlated SSE events and filter enforcement; see [HTTP transport scope](native-mcp-http.md). Hosted trusted synthetic login/callbacks and durable encrypted grants are verified. Manual renewal service and console/Settings controls have separate acceptance in [renewal](native-mcp-renewal.md) | Reconnection and broader SDK features; trusted renewal for the new source, real authority login, registration/CIMD/DCR, automatic renewal/revocation and running-owner coordination, uncertain grant recovery, remaining HTTP/SSE behavior and full live coding interoperability |
| Permissions and policies | Backend-owned durable grant/deny/cancel and effect ownership have actual file/process/peer fixtures | Complete policy rules, recovery and consistent enforcement across exposed tools/protocols |
| Context compaction | Native Responses checkpoints/counting, session/mid-run and manual/automatic paths, private provider state and actual budgets passed their scoped complete gate, including the later serializer repair; [design and evidence](native-context-compaction-design.md) | Complete non-Responses provider strategies and live coding/installed thin-client acceptance using the current package format; local contracts do not establish full compaction completion |
| Snapshots and recovery | Durable uncertainty inspection and non-replay safeguards cover selected effects | General reviewable snapshots, restoration and attributed reconciliation |
| Attachments and references | Thin VS Code selection captures a workspace-bound, labelled buffer as an editable sidebar draft; source, filesystem and archive scopes are recorded in [editor selection](vscode-editor-selection.md). Native CLI work retains [its scope](native-interactive-cli.md) | Installed/rendered selection, native structured file-context/snapshot contracts across clients, complete model-input and multimodal acceptance |
| Formatters and diagnostics | Required by the native coding goal | Rust Analyzer diagnostics, semantic navigation and user-triggered formatting pass the real native and provider-profile runtime contracts; a rendered VS Code Extension Development Host format/save cycle passes and its output passes rustfmt --check. Packaged-extension acceptance, formatter discovery/configuration and model-driven project repair loops remain unverified |
| Search and network configuration | Native HTTP/TLS and workspace search components exist | Configured network/search behavior across coding workflows |
| Session sharing | Local backend sessions are shared across authorized views | Explicit export/sharing behavior and access-controlled output; team sharing belongs to Nexus |
| CLI/TUI | The unified `xmind` console supports authenticated conversation/run/approval/inspection flows, provider catalogue/setup/selection, MCP sign-in/status/watch/cancel, revision-bound admission and signed native history. Windows managed profile discovery/start and shared CLI/view backend ownership pass actual local native contracts | Complete interactive coding/terminal UX, managed profile bootstrap on non-Windows hosts, current installed-package first-use and live provider acceptance |
| Browser and VS Code | The browser exposes a native-root file tree, bounded UTF-8 preview, workspace-bound Git initialization/status and its right-sidebar history, models, approvals and actual metrics within recorded scopes. VS Code now contributes a read-only Explorer Git Changes tree grouped by registered workspace folder, with status and line counts from the shared native backend; see [local contracts](evidence/vscode-git-changes-tree-local-20261013.md) | Editable workspace canvas, complete populated coding, multi-file diff/recovery/context and current rendered/installed editor acceptance |
| A2A and agent graphs | Native shared-executor task controls and durable graphs have scoped contracts; hosted69 direct MCP effects, hosted71 read-only delegation, hosted77 bounded planning/revision and local78 coding-child MCP effects are distinct results. The installed ace/schema-v10 preview completed a real OpenAI two-child read/join; its repaired browser view was rendered. Outbound A2A now retains a streamed task ID across a later event failure and supports read-only inspection after xlang3 SQLite reopen without replay; the native HTTP path verifies opt-in artifact retrieval through response and durable SQLite observation. The independent official A2A JS SDK peer and native client now both verify an actual text artifact part; native/client contracts pass 3/3. Current hosted VS Code/browser contracts pass 328/328 and 105/105 on `70bc353`; these include opt-in artifact-fetch request/rendering and the discoverable sidebar Git menu. Visual Studio 18 Release build and focused A2A client/SDK/registry CTests pass 3/3. [task-recovery evidence](evidence/native-a2a-observed-task-recovery-local-20261011.md) and [artifact-fetch evidence](evidence/native-a2a-artifact-fetch-local-20261011.md) | Installed VS Code delegation and live/rendered/installed planning acceptance, recursive/deterministic dynamic nodes, remote stream resubscription and task recovery when no task ID was observed, live external-peer/model interoperability and remaining protocol acceptance |
| Local/Nexus profiles | Shared protocol and ownership boundaries are specified | Profile isolation, authenticated enrollment and reconnect/lease/recovery acceptance; Nexus implements private coordination |

Themes, warming and additional build/API surfaces need comparison against the
pinned source. Expand the inventory as concrete acceptance requirements are
reviewed.

## Historical native checkpoints

Source `46262d6ef7949a9caf778ccb6cf74733ef28b5ac` passed **52 native, 73
extension and 14 browser contracts** in its
[hosted gate](https://github.com/xlang-foundation/xMind/actions/runs/37709335756).
[Exact evidence](evidence/native-model-protocol-diagnostics-hosted-provenance.json).
Its recorded browser rename, cancel, refresh, reopen and CLI navigation acceptance
is [historical live evidence](evidence/live-browser-cli-navigation-rename.json),
not the current installed-bundle claim. Other actual Responses/tool successes
and the original failed MCP read remain in
[live-provider compatibility](live-provider-compatibility.md).

Graph source `bd059a46781292640d95943e00b0d86a4891936c` passed **44 native and
49 extension contracts** in its
[hosted gate](https://github.com/xlang-foundation/xMind/actions/runs/37682783904).
The actual human/tool graph and restored IDE transcript have their own
[recorded scope](vscode-graph-workflows.md). The earlier foreground-process source
`3297a4bce2d591c0f1c1dd37ccc18a15297369e7` passed **35 native and 40 extension
contracts**; [milestones](milestones.md) preserve its exact evidence. These
versioned results prove bounded components and do not establish full parity.
