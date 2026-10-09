# Native content search

This implementation extends `search_files`. Exact source `5c8c918` passed native
compilation and all 98 hosted contracts in 274.50 seconds. Complete test identities,
receipt/archive hashes and every packaged runtime file were independently checked.
Actual provider-driven regex search and installed-client regex acceptance remain
pending. [Hosted evidence](evidence/native-regex-hosted.json).

The C++ tool accepts `query`, optional `regex` (default false), `case_sensitive`
(default true), `path` (default `.`), `include`, `hidden` (default false),
`respect_ignore` (default true), and `limit` (default 100, range 1–1000).
It searches actual UTF-8 lines and returns workspace-relative paths, 1-based line
numbers and bounded previews. CRLF line endings do not become part of the preview.
Literal behavior remains the default for existing callers; regex is explicit.

RE2 compiles and matches in native C++. Its source is pinned to tag `2025-11-05`,
commit `927f5d53caf8111721e734cf24724686bb745f55`; Abseil is pinned to
`20260817.0`, commit `2065f4ded0558c6f89fee67c8e5228feb4eb960e`.
The imported files retain exact upstream Git blob bytes, licenses and notices.
`Tools/verify-regex-dependencies.mjs` checks pinned manifest hashes, member sets
and all member bytes. Builds use the vendored static libraries, with dependency
tests, fetching and language wrappers disabled. No Python or Node matcher is
used by the product tool.

RE2 syntax supports alternation, repetition, classes, anchors, inline flags and
Unicode properties/simple case folding. Lookaround and backreferences are
rejected with a fixed diagnostic. Matching is per line; multiline flags cannot
join source lines. Its Unicode word/class semantics are RE2's, not an asserted
equivalence to every ripgrep/Rust-regex expression.

Directory searches share the retained-handle discovery walker and local ignore
rules. A positive `include` glob overrides applicable file exclusions, following
the inspected reference adapter. An explicit file target bypasses the discovery
hidden/ignore/include filters. Every target still uses workspace, backend-private
component, reparse-point and hard-link authority checks. Filtering cannot grant
access outside those boundaries.

Query length is at most 4096 UTF-8 bytes; RE2's approximate program/DFA memory
budget is configured to 4 MiB, not a process-wide hard memory ceiling. A file and
aggregate read budget are at most 64 MiB. Actual reads of files later rejected as
binary/invalid UTF-8 still consume the aggregate budget. Files exceeding the
remaining budget are skipped; smaller later files can fit. File validation
finishes before any matching prefix is returned. Previews are at most 4096 UTF-8
bytes, encoded-result accounting stops near 50 KiB, and tool serialization has a
64 KiB hard guard. `truncated`, named `limits`, skipped/ignored entries and source
counters disclose bounded or incomplete coverage. This is separate from the
one-MiB complete-file read/edit snapshot limit.

The pinned OpenCode reference is `v2.0.16`, commit
`3a103fe0aff726a4edc7492f03f7b88195d9e4c9`, specifically
`packages/core/src/tool/plugin/grep.ts` and its native-filesystem/ripgrep adapter.
Its `grep` defaults to regex and names arguments `pattern`, `literal` and
`caseSensitive`. xMind retains its existing `search_files` name and literal
default and adds equivalent choices explicitly. The reference's external-path
authorization, timeout behavior, global ignore configuration and complete
syntax/output parity are not established by this implementation.

Passed native tests exercise actual large files, scoped paths/include globs, Unicode
case folding, malformed/unsupported patterns, an eight-MiB pathological nonmatch,
binary tails, charged binary-read budgets, serialized output limits, strict
argument parsing and private/link boundaries. Further acceptance must execute
the native model-driven tool and verify durable history after xlang3/SQLite
restart. Installed browser/VS Code execution is a separate requirement.
