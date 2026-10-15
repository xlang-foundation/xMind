# Workspace commands

xMind supports workspace-authored prompt commands from `.agents/commands/`.
Each Markdown file is one command; subdirectories become slash-separated IDs.
For example, `.agents/commands/review/security.md` is `/review/security`.

An optional frontmatter block may contain only a plain-text `description` field:

```markdown
---
description: Review a change for security issues
---
Inspect the requested change for security defects. Check the actual workspace
files and report each finding with a file and line number.

Target: $1
Full request: $ARGUMENTS
```

`$1` through `$9` and `${1}` through `${9}` substitute arguments split with
single or double quotes and backslash escaping. `$ARGUMENTS` inserts the
original argument text. Substituted text is inserted once and is never parsed
again as a placeholder. Invocations are bounded to 32 arguments and 8 KiB of
input. A file may be at most 64 KiB; the catalogue is capped at 128 commands
and 2 MiB of source. IDs use ASCII letters, digits, `_` and `-` in each path
segment.

The backend rereads command files at invocation time through the opened
workspace authority. A command is prompt text: xMind does not evaluate shell
snippets, Python, plugins, or other executable content from the Markdown file.
The resulting prompt enters the ordinary single-agent or selected graph run;
tool permissions, approvals, provider routing, metrics and durable history stay
under the regular backend runtime.

Clients use `GET /v1/workspace/commands` to inspect command IDs and public
metadata, then `POST /v1/sessions/{session_id}/commands` with the selected
command, arguments, exact workspace authority, and optional provider/graph
selection. Browser and VS Code composers accept `/command-name arguments` and
resolve the current command catalogue before submission. The interactive CLI
does not yet dispatch workspace commands. An absent command directory produces
an empty catalogue.

This is the native Markdown command-template slice. OpenCode plugins, dynamic
command providers, shell interpolation, command lifecycle events and broader
configuration remain separate parity gaps; see [PARITY.md](PARITY.md).
