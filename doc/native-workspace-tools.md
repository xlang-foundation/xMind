# Native workspace tools

The C++ Windows workspace implementation provides actual file reading, one-directory listing and literal text search, plus typed model tool definitions and exact argument-shape validation. It has no model response generator, HTTP execution route or simulated tool effect. The agent engine still needs to invoke these tools under its policy and durable run lifecycle.

The root is an opened directory handle. File handles are opened, checked against the root's normalized path and then read. Strict normalized path casing avoids treating a case-distinct sibling as the authorized root. Caller paths must be relative; absolute paths, parent traversal, stream/drive syntax and embedded NUL are rejected. Windows extended paths use native separators. Hard-linked files are denied pending a separate explicit alias policy.

Directory listing operates on the verified handle using [GetFileInformationByHandleEx](https://learn.microsoft.com/en-us/windows/win32/api/winbase/nf-winbase-getfileinformationbyhandleex) and [FILE_ID_BOTH_DIR_INFO](https://learn.microsoft.com/en-us/windows/win32/api/winbase/ns-winbase-file_id_both_dir_info), rather than validating a directory and reopening a pathname for enumeration. Directory reparse points are not traversed. Reads verify the final file target, so an outside-workspace junction target is denied. Returned entry metadata includes link/directory/file kinds.

Read limits are 1 MiB per UTF-8 text file; binary/NUL-containing or invalid UTF-8 files are rejected. Listings return up to 1000 entries with `truncated`. Search skips `.git`, `.venv`, `.agentflow`, `node_modules` and `__pycache__`, follows no listed links and reports skipped entries. Search bounds are 10000 inspected entries, 64 MiB of file content and 100 matches. Each preview is at most 4096 UTF-8 bytes and carries `text_truncated`. Directory and total search truncation remain explicit; absence of a match in a truncated search is not a complete negative result. Cancellation is checked between directory/file/read/line operations.

## Verified scope

The native tools contract passed against independently created real filesystem fixtures: nested UTF-8 and normal Windows case lookup, literal path/line matches, Unicode preview clipping, argument validation, cancellation, concurrent reads, oversized/binary rejection, outside-root junctions (including a nested target), hard links, excluded directories and truncation. Junction creation failure fails the test rather than skipping boundary checks. The outside marker remains unchanged. No real or synthetic model was invoked by this filesystem test.

The first full run exposed a nested-path separator issue; the corrected implementation passed the targeted tools contract. The other ten native contracts passed during that full run. Current tools evidence: [native-workspace-tools-ctest.log](evidence/native-workspace-tools-ctest.log).

Writes, patch review/recovery, shell/PTY, Git, diagnostics, ignore-file semantics beyond the listed exclusions, paginated listings, other OS implementations, tool permissions/approvals and agent integration remain required. The read-only library does not establish a complete coding agent or OpenCode parity.
