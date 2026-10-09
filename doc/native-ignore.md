# Native workspace ignore rules

The C++ discovery walker and literal content search share workspace-local ignore
rules. `.ignore` and `.rgignore` apply without requiring a repository. A detected
regular `.git` directory or worktree marker enables local `.gitignore` rules;
an in-workspace `.git/info/exclude` supplies lower-priority rules. Nested repository
markers start a new Git-rule scope while retaining applicable search-ignore rules.

Precedence is `.rgignore`, `.ignore`, `.gitignore`, then `info/exclude`. Within
one source category, deeper directory rules override ancestors and the last
matching rule wins. Rule locations anchor their relative paths. Blank lines and
comments, negation, leading/trailing slashes, escaped characters and trailing
spaces follow Git-style interpretation. Recursive tails match descendants rather
than prematurely excluding their parent. Named character classes use ASCII ranges;
matching remains case-sensitive and locale-independent. These are behavior
references from the [Git manual](https://git-scm.com/docs/gitignore) and
[ripgrep guide](https://github.com/BurntSushi/ripgrep/blob/master/GUIDE.md),
not imported platform implementations.

`glob_files` preserves the pinned OpenCode adapter's explicit-glob behavior:
positive matches override file/directory exclusions, but an ignored parent that
does not match the positive glob is pruned. A leading `!` is an exclusion glob;
its remaining candidates use ordinary ignore filtering. `\!name` matches a literal
exclamation mark. An explicitly scoped search directory is admitted directly,
with ancestor rules inherited for its entries. `respect_ignore=false` bypasses
ignore metadata; it does not bypass workspace authority, hidden-file choice,
backend-private components, `.git` exclusion or link checks.

`search_files` uses ordinary filtered traversal rather than a positive-glob
whitelist. It searches visible UTF-8 files and removes the old fixed dependency
directory list; repository rules now determine which dependency/build directories
are excluded. Its existing per-file 1 MiB, total 64 MiB, 10,000-entry and
100-match bounds remain. Result `ignored_entries` and `ignore_files` are actual
traversal counters; an intentional rule exclusion does not imply incomplete
coverage. Unreadable/binary/large files and other skipped coverage remain explicit.

Metadata is opened relative to retained native directory handles. Each source
must be a regular single-link UTF-8 file without NUL; links and unreadable sources
do not become permissive empty rules. Failure at the search root fails the tool;
failure in a descendant excludes that subtree and reports incomplete coverage.
No patterns, comments or file contents are returned to models. Rule snapshots are
observations during this call, not a filesystem transaction or an edit grant.

Limits are 32 KiB per metadata file, 256 KiB total metadata and 4,096 processed
rules per discovery call. Matcher limits and the shared 50-million-step budget
also apply. A malformed wildcard never matches; matching-capability exhaustion
does not silently drop an applicable rule. Cancellation remains cancellation.

User/global ignore files, parent rules outside the authorized workspace and
external worktree metadata are not read automatically. Trusted global-ignore
configuration, full Git/ripgrep compatibility, regular-expression content search,
large-file content search and installed/editor acceptance remain incomplete.

The final candidate passed **98 native contracts in 209.78 seconds**, with all
637 mapped inputs unchanged and exact expected/registered/passed manifests
matched, plus **215 extension / 39 browser** contracts. Native HTTP/CLI checks
persist actual filtered discovery and content-search results across xlang3/SQLite
restart. Their inference peers remain synthetic.

A separate actual OpenAI `gpt-6.1-sol` run searched the owned fixture once,
received and quoted its two nonignored paths, reported two provider-supplied
usage messages and retained exact history after restart. Generated/excluded and
private configuration files were absent from the results. No mutation operations
were created; the installed VS Code profile was not changed.

The first independent ripgrep corpus comparison failed on POSIX character classes
and literal braces. Actual Git ignore decisions, combined with the explicitly
fixture-defined higher-priority search overrides, match the native corpus.
Installed ripgrep 15.2.0 returns two additional paths for those dialect cases;
that difference remains recorded rather than presented as full parity. The native
implementation retains the documented Git interpretation. See
[exact source/gate/live/reference evidence](evidence/native-ignore-local.json).
