# Native file discovery

`glob_files` finds file paths through the same native workspace authority used
by coding agents. Its input is `pattern`, optional search-directory `path`
(default `.`), optional `hidden` (default false), and optional `limit`
(default 100, range 1–1,000). It reads names and file attributes, not contents,
and creates no mutation proposals or approvals.

Patterns support `*`, Unicode-scalar `?`, recursive directory component `**`,
character classes and ranges such as `[abc]`, `[1-3]` and `[!abc]`, and brace
alternatives such as `{cpp,hpp}`. Matching is case-sensitive. A basename
pattern such as `*.cpp` matches files at any depth. A pattern containing a
slash is relative to the search directory: `src/**/*.cpp` also matches
`src/main.cpp`, because `**` can consume zero directories. Paths returned by a
scoped search remain relative to the authorized workspace, not to its `path`.

The result contains `paths`, `truncated`, `limits`, `scanned_entries`,
`scanned_directories` and `skipped_entries`. Returned paths are sorted; when
coverage is bounded they are the subset found by sorted directory traversal,
not a claim about every match in the workspace. An extra matching file beyond
the requested limit marks `result_limit`. Unsafe or inaccessible entries are
skipped and explicitly mark incomplete coverage rather than becoming an empty
successful search.

C++ retains opened directory handles from the canonical workspace root through
the chosen search directory and every active traversal branch. Child entries
are opened relative to those handles. Junctions, other reparse points and
hard-linked files are excluded. Final opened paths and the workspace identity
are revalidated. `.config` and `.agentflow`, including case/trailing-dot/space
aliases, always remain private. `.git` remains excluded even with `hidden=true`.
Public dotfiles and files carrying the Windows hidden attribute are included
only when requested. Discovery does not authorize a later file read or write.

One enumeration buffer serves all traversal frames, so deeper paths do not
consume 64 KiB of thread stack per level. An actual 32-level fixture verifies
the admitted edge file and disclosed exclusion of a deeper directory.

The compiled matcher uses dynamic programming rather than a regex engine or
recursive backtracking. Limits are 1,024 pattern bytes, 32 path components,
256 tokens per component, 32 ranges per character class, 64 expanded alternatives
and four brace-expansion layers. Traversal is bounded to 20,000 directory
entries, 2,000 directories and 32 descent levels, with 50 million matching
steps and a conservative 50 KiB encoded path budget. Results disclose the
applicable bound; cancellation never becomes successful partial completion.

Ordinary agent catalogues, bounded read-only children, dynamic preset validation
and deterministic graph tool admission recognize this same native tool.
Existing persisted capability snapshots still bind their original catalogues;
discovery does not widen an old snapshot or add effect authority.

The behavior reference is OpenCode v2.0.16 at
`3a103fe0aff726a4edc7492f03f7b88195d9e4c9`, specifically its glob plugin,
filesystem input contract and ripgrep adapter. xMind implements its own C++
matcher and handle traversal. It does not execute or embed OpenCode/ripgrep.
Gitignore/global-ignore rules, escaped literal metacharacters and full search
parity remain incomplete.

The final source passed all 98 native contracts in 208.70 seconds, with 635
mapped inputs unchanged and exact expected/registered/passed manifests matched.
The unchanged clients passed all 215 extension and 39 browser contracts.
An actual OpenAI `gpt-6.1-sol` run discovered and quoted four exact paths,
including Unicode and public hidden filenames, while excluding a synthetic
private `.config` file. It created zero mutation operations, and complete
discovery history survived an embedded-xlang3 SQLite restart. The first full
native attempt had four stale synthetic catalogue assertions; their exact
expectations were updated. That failure and the later stack-layout correction
remain recorded in [native-glob-local.json](evidence/native-glob-local.json).

Filesystem, native HTTP/CLI, delegated-child and graph contracts use actual
disposable files; their inference peers are synthetic. They do not establish
installed/rendered editor acceptance. The separate live proof above covers
one provider/read-only discovery path. Boundary tests cover
pattern syntax, Unicode, hidden attributes, result truncation, links/private
paths, actual depth exhaustion, cancellation and matching-budget exhaustion; not every traversal limit
is driven to exhaustion on disk. Accepted results are recorded separately.
