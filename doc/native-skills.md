# Native workspace skills

This checkpoint adds C++ discovery and per-run activation of workspace skills to
the actual agent tool loop. The source is unvalidated and is not installed in the
preview. Its complete native gate, packaging and live-provider acceptance remain
pending. It does not establish OpenCode skill parity.

The behavior reference is the pinned OpenCode `v2.0.16` source, commit
`3a103fe0aff726a4edc7492f03f7b88195d9e4c9`: skill discovery describes available
guides, activation loads task guidance with its base directory, and model
invocation can be disabled. The implementation here is original C++; it does not
execute or embed OpenCode. The broader pinned reference includes directory,
URL and embedded sources and skill catalog events, which remain unfinished here.

## Current source behavior

Native discovers `.agents/skills/<id>/SKILL.md` inside the already authorized
workspace. It uses retained Windows directory handles for optional discovery and
verified file snapshots for content. Missing skill roots are empty catalogues;
unreadable, linked, malformed or oversized sources are explicit errors. Backend
private directories remain outside workspace authority. Discovery does not
execute files, import scripts, read provider configuration or grant effects.

A skill has YAML frontmatter with `name` matching its directory and an optional
`description`, followed by a Markdown body. An absent description stays absent in
catalogue metadata and excludes the guide from automatic suggestions; explicit
loading by an available id remains supported. This follows the pinned schema's
optional description and instruction catalogue filtering. Names use lower-case letters, digits
and single hyphens, up to 64 bytes. Frontmatter supports bounded scalar fields
and one scalar metadata map. Native rejects aliases, anchors, duplicate keys,
tagged objects and sequences. Other scalar metadata grants no capability.
`autoinvoke: false` and `disable-model-invocation: true` keep the guide out of
the advertised model-invocable catalogue. User-driven activation of those guides
is still pending.

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

Limits are 64 catalogue entries, 16 KiB per source, 8 KiB of frontmatter, 1,024
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
catalogue; existing effect and protocol assertions remain in place. These tests
have not run yet, so their presence is not evidence that the feature works.
The agent fixture also covers a guide larger than its remaining instruction
capacity and verifies a completed conversation after the rejected load, with no
activated skill snapshot. The context fixture covers aggregate overflow while
retaining two previously delivered guides. These additions remain unvalidated.

Delivery still requires the complete 90-contract native gate, frontend suites,
browser/native integration, independently verified VSIX/runtime artifacts and
installed/live acceptance. Durable activation restoration across resumed or new
runs, explicit manual activation and deactivation, configured/global/URL/embedded
sources, skill management events and CLI/view catalog controls remain work toward
the full skill experience. Pure Python scripts and dependencies must execute
through xlang3; this checkpoint adds no interpreter fallback or xlang3 native API.
