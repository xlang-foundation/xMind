# Native text file pages

The native `read_file` tool accepts optional `offset` and `limit` arguments.
Offsets are 1-based; the limit defaults to 2,000 lines and must be between 1
and 2,000. Supplying either argument enables paging. A path-only call keeps
the existing complete-file behavior and its 1 MiB maximum.

For example, `{"path":"src/main.cpp","offset":101,"limit":40}` returns
`content`, `offset`, `lines_read`, `has_more`, `truncated`, and
`truncated_lines`, alongside the relative `path`. When more lines remain,
`next_offset` identifies the next unread line. There is no guessed total
line count. A trailing newline terminates a line rather than introducing
another empty line; an empty file at offset one returns zero lines. An offset
beyond EOF is an error.

C++ opens and reads a verified native Windows file handle. Paging supports
files up to 64 MiB without loading the file into memory. It validates UTF-8
across read buffers, skipped lines and clipped line contents. NUL, malformed
UTF-8, hard links, paths outside the workspace and backend-private `.config`
and `.agentflow` components are rejected. The open handle excludes concurrent
writers and deletion. File and workspace paths are revalidated before returning.

Source bytes, including CRLF and an unterminated final line, are preserved
except for explicitly clipped lines. Each line retains at most 2,000 Unicode
scalar values; clipped line numbers appear in `truncated_lines`. Clipping
never splits a UTF-8 code point. The reader uses a conservative 50 KiB encoded
output budget that accounts for JSON escaping, paths and line metadata;
serialized tool results are capped at 64 KiB. A page can therefore contain
fewer lines than requested. Its continuation resumes at a complete line.
`truncated` also reports clipped lines at EOF, even when `has_more` is false.

A page is an observation, not a complete edit snapshot or an effect grant.
Existing file edits still bind their proposals to complete file identities,
content hashes and explicit approval. Paging does not extend the edit-size
limit, grant writes or change the installed backend's policy.

The behavior reference is OpenCode v2.0.16 at commit
`3a103fe0aff726a4edc7492f03f7b88195d9e4c9`, specifically
`packages/core/src/tool/plugin/read.ts` and `packages/core/src/tool/read-filesystem.ts`.
Only its behavior was inspected; the implementation is native xMind C++.
Directory paging, image/PDF reads, recovery of the omitted suffix of an
oversized line, and full coding-tool parity remain incomplete.

Validation uses actual disposable files in the native workspace contract and
the native server/CLI agent contract. The HTTP inference peer is synthetic;
the C++ execution, file reads, tool continuation and embedded-xlang3 SQLite
history/restart are real. These contracts do not establish live provider or
installed/rendered VS Code acceptance. Gate results are recorded separately
in [native-file-pages-local.json](evidence/native-file-pages-local.json).
