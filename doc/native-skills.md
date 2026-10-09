# Native workspace skills

This checkpoint adds C++ discovery and per-run activation of workspace skills to
the actual agent tool loop. Source `d767d506` passed the complete 90-contract
native gate and hosted packaging. Independent verification binds the tested
runtime and VSIX to that source. The new package is not installed in the preview;
installed and live-provider skill acceptance remain pending. It does not
establish OpenCode skill parity.

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
Installed/rendered acceptance remains pending. No local runtime, provider or
editor was executed for these isolated results.

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
this scope. It does not claim local execution, installation or rendered/live
skill acceptance. Earlier failed gates remain recorded above.

Delivery still requires installed/live acceptance. Durable activation restoration across resumed or new
runs, explicit manual activation and deactivation, configured/global/URL/embedded
sources, skill management events and CLI/view catalog controls remain work toward
the full skill experience. Pure Python scripts and dependencies must execute
through xlang3; this checkpoint adds no interpreter fallback or xlang3 native API.
