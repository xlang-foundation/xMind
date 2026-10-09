# Native workspace skills

This checkpoint adds C++ discovery and per-run activation of workspace skills to
the actual agent tool loop. Source `d767d506` passed the complete 90-contract
native gate and hosted packaging. Independent verification binds the tested
runtime and VSIX to that source. The package is installed in the persistent
VS Code profile, passed a real isolated opened-folder host and passed skill
activation/companion reads with four real providers. The browser preview now
uses this runtime and preserved its saved records during replacement. Normal
VS Code rendered acceptance and full skill delivery remain
pending; this does not establish OpenCode skill parity.

The behavior reference is the pinned OpenCode `v2.0.16` source, commit
`3a103fe0aff726a4edc7492f03f7b88195d9e4c9`: skill discovery describes available
guides, activation loads task guidance with its base directory, and model
invocation can be disabled. The implementation here is original C++; it does not
execute or embed OpenCode. The broader pinned reference includes directory,
URL and embedded sources and skill catalog events, which remain unfinished here.

## Current source behavior

Native discovers root Markdown guides and nested `SKILL.md` files under
`.agents/skills` inside the already authorized workspace. Root `<id>.md` files
use the filename stem as their id; a nested `SKILL.md` uses its containing
directory's name. A root `SKILL.md` uses `skills`. It uses retained Windows
directory handles for optional discovery and
verified file snapshots for content. Missing skill roots are empty catalogues;
unreadable, linked, malformed or oversized sources are explicit errors. Backend
private directories remain outside workspace authority. Discovery does not
execute files, import scripts, read provider configuration or grant effects.

A skill has YAML frontmatter with optional `name` and `description`, followed by
a Markdown body. The display name defaults to its file-derived id and may differ
from that id. An empty frontmatter document uses those defaults. An absent description stays absent in
catalogue metadata and excludes the guide from automatic suggestions; explicit
loading by an available id remains supported. This follows the pinned schema's
optional description and instruction catalogue filtering. Catalogue ids retain
the actual source spelling, including case, spaces and Unicode, up to 256 UTF-8
bytes. Ids resolve through the discovered catalogue and never become a caller's
filesystem path. Display names have the existing 1,024-byte scalar bound.
Frontmatter supports bounded scalar fields
and one scalar metadata map. Native rejects aliases, anchors, duplicate keys,
tagged objects and sequences. Other scalar metadata grants no capability.
`autoinvoke: false` hides automatic suggestions while allowing explicit loading
by an available id. The pinned reference's `metadata.opencode/autoinvoke` flag
has the same meaning; its string value is trimmed and normalized. The native
top-level alias accepts `true` or `false` and conflicting declarations fail
explicitly. `disable-model-invocation: true` remains an explicit native loading
prohibition. User-driven attachment controls for disabled guides are pending.

`list_skills` returns current metadata. `load_skill` accepts an exact catalogue
id and requests activation for the next model request. Skill bodies enter the
backend-owned instruction context rather than being promoted from arbitrary tool
result prose. A later tool in the same activation batch defers until that current
guidance has reached the model. Companion files use ordinary workspace tools
relative to the supplied base directory; script execution requires its own
approved native process policy and cannot be inferred from a guide.

Active source identities and content hashes join the native run's repository
scope events. File creation, edits, process dispatch and agent MCP proposals bind
active skill snapshots into approval metadata and recheck them after approval.
A changed source retires the proposal before dispatch. Loading cannot override
native permissions or satisfy an approval. Skills are available to ordinary
agents and bounded agent children; deterministic graph tools retain their own
execution contracts.

Limits are 64 catalogue entries, 512 visited directories, 8,192 enumerated entries
and 30 nested directory levels. Discovery rejects truncation and duplicate ids
explicitly; it does not silently select one conflicting source. Other retained
workspace path and private-state checks remain applicable. Additional limits are
16 KiB per source, 8 KiB of frontmatter, 1,024
bytes per scalar, eight active guides and 32 KiB of active body text. Serialized
catalogue, guidance and combined instruction limits also apply. Oversized
catalogues fail explicitly instead of silently returning a partial set.
Activation validates the prospective active body total, serialized guidance and
remaining combined native instruction budget before changing the requested set.
A rejected load preserves existing delivered guidance and lets the agent handle
the tool error. Source changes after activation are still rechecked separately.

## Validation and remaining delivery

The new `native_skill_context_contract` exercises actual filesystem discovery,
bounded parsing, delivery barriers, refresh and approval snapshots. The existing
native agent runner contract gains a synthetic provider sequence that activates
a guide, verifies same-batch deferral and reads actual companion bytes. The MCP
effect contract gains a changed-skill-after-approval case with an actual peer and
durable operation retirement. Host fixtures assert the complete new tool
catalogue; existing effect and protocol assertions remain in place.

The first hosted gate, source `045779e3c8c487e01b1512bdd7a86f45b5bf477a`,
[run 37879098352](https://github.com/xlang-foundation/xMind/actions/runs/37879098352),
compiled successfully and passed 84 of 90 native contracts. The new skill
context, agent activation/delivery barrier and changed-skill MCP approval
contracts passed. The complete gate failed: dynamic-plan MCP, delegation,
DeepSeek provider, agent MCP HTTP and both upstream MCP SDK contracts failed.
[Verified failed-gate receipt](evidence/native-skills-initial-hosted.json) binds
the archive digest, pinned build sources, exact complete test registration and
original log hashes. The [90-result projection](evidence/native-skills-initial-test-results.json)
contains parsed test names, statuses and timings; it is explicitly a projection,
not a copy of raw diagnostic output or evidence for later fixes.
The HTTP/SDK shared fixture, delegated-leaf catalogue and DeepSeek engine
fixture still expected the former four native reads. Their strict expectations
now include the two skill tools, preserving the existing effect and protocol
checks. Source inspection identified a separate dynamic-plan MCP defect: its
repository authority boundary still required a ten-field proposal after native
instruction snapshots added an eleventh field. The boundary now accepts that
optional snapshot with a bounded exact native shape, while retaining equality
checks for all nine sealed registry fields, exact raw arguments and the captured
server resource. Repository contracts cover malformed snapshots, registry drift
with guidance and preservation of valid guidance metadata; their source additions
were subsequently covered by the successful complete gate below. The integration fixture reports bounded request progress,
native lifecycle states and assertion locations without printing provider or peer
payloads. The successful complete gate below includes these corrections.
The agent fixture also covers a guide larger than its remaining instruction
capacity and verifies a completed conversation after the rejected load, with no
activated skill snapshot. The context fixture covers aggregate overflow while
retaining two previously delivered guides. The second hosted source
`50750656e43cd51107ebe92e4331fa07c93ca440`
[run 37880927365](https://github.com/xlang-foundation/xMind/actions/runs/37880927365)
compiled and passed the skill context, agent runner and MCP effect contracts,
including the capacity and optional-description cases. Its complete gate still
failed with the same six failures, 84/90 in 325.86 seconds; it predates the
fixture and dynamic MCP proposal fixes. The
[verified failed-gate receipt](evidence/native-skills-capacity-hosted.json) and
[complete result projection](evidence/native-skills-capacity-test-results.json)
retain that scope. These individual contracts do not establish full delivery.
The later automatic-suggestion correction is included in that gate. Its native
contract covers explicit loading with `autoinvoke: false`, the normalized pinned
metadata flag and the separate explicit loading prohibition. It changes no
effect authority or source snapshot verification.
The subsequent format correction adds contract cases for root
Markdown files, nested `SKILL.md`, optional/custom display names, empty metadata,
case/space/Unicode and longer ids, duplicate-id refusal and changed flat-source
approval snapshots. Configured source precedence and broader source providers
still need implementation and acceptance.
The shared browser/VS Code approval renderer now summarizes active skill ids,
source paths, sizes and hashes alongside repository guidance. It recognizes MCP
`instructions` and file/process `repository_guidance` metadata, disables Allow
when those bindings are malformed and sends only the operation id and decision.
Its new DOM contract covers all four effect adapters, safe literal rendering,
workspace mismatch, bad hashes/ids/counts and inspection of uncertain outcomes
without offering another approval. The isolated source `d767d5069f52e3e6b899ae7a3e8313ebcc0ecbaf`
[view run](https://github.com/xlang-foundation/xMind/actions/runs/37883217731)
passed all 176 extension and 35 browser contracts with all 41 source/vendor files
and 11 assets unchanged. The workflow still failed because its fixed expected
extension count remained 175 after adding the new renderer contract. After the
helper was corrected to require exactly 176, source `dcfc4415696dff9dac175c5a44f7375c39e08a25`
[run 37883391612](https://github.com/xlang-foundation/xMind/actions/runs/37883391612)
passed the complete workflow with 176/176 extension and 35/35 browser contracts.
[Independent view evidence](evidence/native-skill-approval-views-hosted.json)
retains both workflow conclusions, verifies all archive bytes, matches the 39
tracked view sources to each exact Git revision and binds all 11 tested assets.
The isolated results themselves executed no local runtime, provider or editor;
the later installed/live checks below have a separate scope.

Source `d767d5069f52e3e6b899ae7a3e8313ebcc0ecbaf`
[run 37883217775](https://github.com/xlang-foundation/xMind/actions/runs/37883217775)
passed the complete workflow: all 90 native contracts, frontend suites,
browser/native integration, runtime staging, VSIX verification and artifact
publication. Independent file verification matched every registered native
contract to a passed result, checked both advertised archive digests, all 30
accepted bundle files and all 1,861 packaged runtime inventory files. Seven
host sources and five copied view sources match the exact commit, and two
vendor copies match the packaged dependencies.
[Verified package receipt](evidence/native-skills-hosted-package.json) records
this file-only scope. Later installation and actual execution have the separate
receipt below. Earlier failed gates remain recorded above.

## Installed and real-provider acceptance

The actual Code CLI installed that exact VSIX into the existing persistent
profile. All 1,861 runtime files, 19 other package files and 12 browser-runtime
files matched; settings bytes were unchanged and no trust override was requested.
A fresh isolated VS Code host then opened `D:\CantorAI2026\TestProj` directly,
activated the extension and verified the native authenticated workspace root.
No `.code-workspace` was needed. The isolated test profile used the standard
trust-disabled test flag; the normal profile's trust settings were unchanged.

Fresh real OpenAI, Claude, Gemini and DeepSeek runs used the installed native
runtime and production provider controller. Each explicitly activated a guide
with `autoinvoke: false`, received a delivered skill snapshot bound to its
identity/hash and read an owned companion file whose random marker was absent
from the prompt and guide. All four replies included those actual file bytes;
there were no effect-operation events. The first strict-format attempt passed
three of four replies: Claude added prose and a code fence. That harness failed
and also reported `EBUSY` because cleanup did not wait for backend exit. A
Claude-only retry added an explicit final-reply format instruction and waited
for backend exit before cleanup; it passed with the exact marker. The original
failure is retained in the receipt rather than reported as a passing batch.
Owned guides and marker files were removed, and the original leftover empty
workspace was removed after verifying its exact path and emptiness.

[Installed/live evidence](evidence/native-skills-installed-live.json) checks the
private native histories, actual calls/results, delivered snapshot events and
real usage/timing fields. It publishes metadata and hashes only. These checks
do not establish rendered browser/IDE acceptance, normal-window reload or
durable activation restoration. The subsequent browser upgrade has separate
evidence below.

## Browser upgrade and approval stability

The same accepted native runtime and packaged browser assets replaced the owned
preview at `http://127.0.0.1:60405/ui/`. A disposable instance first exercised
the helper's replacement, startup and durable-cookie checks. The live instance
was idle before stopping its view and native owners, copying a closed database
backup and starting the verified replacements on the same addresses. Exact
pre/post API snapshots matched 20 sessions, 27 root runs, 57 history rows, three
operation journals, two graphs/two graph children, two delegated children and
four planning roots, including all configuration and paginated events. No
CPython interpreter or direct SQLite client was used.

Actual page reload restored its connection, saved conversation and all 20
OpenAI model choices without entering a token. Compact 319×431 and desktop
1024×768 observations covered the footer controls and right sidebar. A fresh
real browser run loaded an owned skill, read its companion and proposed file
creation. The approval displayed the actual guide id, source path, byte count
and hash. Polling then rebuilt the operation card and collapsed the expanded
guidance; during a click the moving control opened Compare changes instead.
The run was stopped before approval, its operation was durably cancelled and
the result file was absent. Owned source/companion fixtures were then removed.
This is a failed approval-interaction check, not a completed coding effect.

The source fix retains unchanged operation DOM nodes, expanded guidance,
keyboard focus and in-flight decision controls across polls. Changed native
bindings replace the card, and cached expired approvals are still disabled.
Uncertain-file inspection results also survive identical updates. The renderer's
58 contracts passed, including a new regression case covering file, command and
MCP cards. Source `5d48bdd72713dcc23d5a1b15a0a08d3e3b7b1fd2` then passed
[the complete 177-extension / 35-browser workflow](https://github.com/xlang-foundation/xMind/actions/runs/37886895004).
[Independent view evidence](evidence/native-approval-stability-views-hosted.json)
verifies the advertised archive digest, every evidence file, raw suite counters,
39 pinned Git sources, two vendor copies and all 11 tested assets. The verified
`chat.js` replaced the preview's script; its other ten assets matched the same
gate. A validated complete page load refreshed the adapter's asset snapshot,
and the served script matched the tested digest. Neither process restarted.
[Browser upgrade and cancelled-proposal receipt](evidence/native-skills-browser-upgrade.json)
retains both the successful state-preserving upgrade and the UI failure. New
test admission has occurred; the pre-upgrade database backup must not replace
the current database.

The actual browser retry passed. Skill details stayed expanded across native
polling; clicking Allow creation approved the intended owned proposal. The
native operation succeeded, the created file contained the exact companion
bytes and the real OpenAI run completed with the exact marker. Reload restored
that completed run, its response, actual usage/timings and all 20 model choices
without entering a token. The guide, companion and created result were removed
only after verifying their exact owned paths and contents.
[Live approval evidence](evidence/native-approval-stability-live.json) retains
the original cancelled run alongside the successful retry. The
[completed-page screenshot](evidence/native-skill-browser-completed.png) shows
the actual compact sidebar response. This browser check does not establish
rendered IDE acceptance. The fixed package subsequently passed the complete
90-contract native workflow and was independently verified and installed into
the persistent VS Code profile. Its complete installed inventory matched and
settings remained unchanged; no trust bypass was requested.
[Package and installation evidence](evidence/native-approval-stability-hosted-package.json).

## Native catalogue inspection

The subsequent source exposes `GET /v1/workspace/skills`, `xmind_cli PORT skills`
and `/skills` in console chat. The shared view client has a `skills()` method;
the browser adapter forwards the exact GET route using its scoped native view
credential. Native discovers current metadata through the retained workspace
handle and includes its workspace/generation identities. This works before
provider enrollment or model selection. It returns names, descriptions and
invocation flags, without guide bodies, activation, effects or provider access.
Malformed or ambiguous sources reject the whole inspection instead of
returning a partial catalogue. Query parameters and browser writes to this
route are rejected. Manual attachment/removal and rendered catalogue controls
remain pending. The exact complete 92-contract local native suite and the
178-extension / 36-browser local suites passed with unchanged source maps.
The catalogue contract exercises actual native configured/unconfigured-provider
roots, the CLI command and console chat, current metadata, malformed/ambiguous
source rejection and scoped native/browser cookie access. Its guide content,
endpoint and unrendered adapter assets are explicitly synthetic fixtures;
there is no inference or rendered UI claim. Two earlier harness failures are
retained: `fetch` normalized an empty query away, and a later assertion queried
a nonexistent global run-list route. The final test uses raw HTTP for that exact
query target and checks the actual session-list API before and after inspection.
[Catalogue evidence](evidence/native-skill-catalogue-local.json).

## Durable native selections

Schema13 adds typed session/workspace and run skill selections. It stores only
model-activated guide identities, never cached bodies or permissions. A
successful native `load_skill` acknowledgement commits its selection in the
same transaction as the assistant call and all matching results. Cancellation,
an unmatched batch, a foreign tool's acknowledgement-shaped output or a failed
transcript commit cannot publish a new selection. Generic telemetry cannot
write skill lifecycle events.

A new prompt inherits this session's selections for the actual workspace
identity. A child inherits its parent run's selection, or the session selection
for a graph agent; later child activations remain local to that child. Existing
run selections survive reopening the repository. Before the first model request,
the runner discovers the current sources and renders their current bodies.
Missing or no-longer-invocable guides fail with `skill_context_unavailable`
instead of supplying cached instructions. Explicit clearing controls remain
pending; restore the guide or start a new conversation to avoid its selection.
Migration does not infer selections from historical messages or events.

The complete local 91-contract native build passed with unchanged source maps
and the exact expected CI test names. Tests include real embedded-xlang3
transaction rollback, repository reopening, continued-session restoration,
workspace binding, graph-child isolation and current-source restoration. Their
provider replies and activation acknowledgements are labeled synthetic.
The first selected attempt failed because a second constructor version check
still rejected schema13; its failure and the successful retry are retained.

Two actual OpenAI runs additionally passed native backend exit/restart and
continued-session restoration. After exit, the owned guide was changed to name
a different random companion file. The continued run received the new guide
hash, read that file and returned its exact contents without another
`load_skill` call. The provider profile authority stayed unchanged, and neither
run requested effects. The original live harness re-saved the model on restart,
which rotated provider authority and caused `context_unavailable` before any
new provider request. The corrected harness reused its persisted configuration;
both attempts remain in [local evidence](evidence/native-skill-state-local.json).
The successful retry's owned sources and files were removed after verification;
the failed attempt's isolated fixture was subsequently moved from `TestProj`
into private diagnostic storage, retaining all three files and failed records.

This schema13 source is locally validated, not yet installed or packaged by the
hosted workflow. The current persistent VS Code package and browser runtime use
schema12. Delivery still requires normal VS Code rendered acceptance and window
reload, installed schema13 acceptance, paused-run restoration checks, explicit
manual activation and deactivation, configured/global/URL/embedded sources,
skill management controls and CLI/view catalog controls. Pure Python scripts
and dependencies must execute through xlang3; this checkpoint adds no interpreter
fallback or xlang3 native API.

The actual normal VS Code profile was inspected after the fixed schema12 package
installation. Its `TestProj` window is in Restricted Mode. xMind's manifest
declares `untrustedWorkspaces.supported: false`, so VS Code disables its sidebar.
The user must trust this folder through VS Code before ordinary-window xMind
activation and rendered acceptance can continue; automation did not change trust
or other security settings. The
[observed window](evidence/native-vscode-restricted-mode.jpg) records that boundary,
not a rendered xMind result. The earlier isolated test host's trust-disabled
test flag does not establish ordinary-window acceptance.
