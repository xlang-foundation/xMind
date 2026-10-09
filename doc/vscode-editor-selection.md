# Workspace-bound editor selection

`xMind: Ask About Selection` now adds selected editor text directly to the
right-sidebar composer. It preserves a question already typed there, focuses
the composer and submits no model request. The extra question popup is removed.

The thin VS Code input adapter binds the captured document to the connected
backend workspace before reading its buffer. It checks canonical file paths,
regular-file/link metadata and repository-private path exclusions. Another
workspace root, a canonical sibling with different case, an alias outside the
root, `.config` and `.agentflow` cannot supply selection context. Untitled and
virtual documents are explicitly unsupported.

The captured document's version and selection must remain unchanged while
filesystem metadata is observed. A changed workspace, connection, document,
selection or path rejects the draft. The sidebar can take focus without
discarding the document captured at command invocation. The host queues context
until the view's actual ready handshake has published its workspace root;
reconnection or disposal clears pending context. A selection is delivered once.

The draft contains a relative path, range, language and exact JSON-quoted text.
Unsaved editor buffers are labelled. Selected text is bounded to 32 KiB and is
never silently truncated. Appending context also has a combined draft limit;
exceeding it preserves the existing question. The shared renderer accepts
context only for its current displayed workspace and treats it as plain text.

This is user input, not a native filesystem snapshot or a grant to execute file
effects. The native C++ engine still owns admission, model/tool execution,
repository guidance, approvals and persistence. The editor adapter neither
saves the document nor modifies project files. Structured native file-context
attachments, snapshot binding and context parity across CLI/browser/editor
remain separate requirements.

All **215 extension and 39 browser tests** passed with **43 view inputs** and
11 browser assets unchanged. These editor/controller/DOM fixtures are synthetic;
they verify workspace, stale-state, initial-handshake, retained-view reconnection
and composer behavior without an installed IDE or inference.

A separate actual Windows filesystem check verifies canonical paths, exact
Unicode buffer preservation, private-path rejection and rejection after creating
a real hard link. Its editor document and backend metadata remain explicitly
synthetic. It does not establish native or installed editor admission.

The initial reconnection fixture incorrectly waited for a second sidebar view
and timed out after 214 of 215 tests passed. Its corrected version observes the
retained view, rejects its retired callback and checks that pending old-root
context is discarded. Earlier source/archive checks before the focus and ready
handshake reviews remain retained; they were not installed or used as rendered
acceptance.
[Evidence and scopes](evidence/native-editor-selection.json).

The verified **0.1.3 VSIX** has 1,934 entries. Eleven host/view files match the
final source, all runtime bytes match the unchanged accepted 1,899-file manifest,
and all 18 required browser/access/license assets are present. The native runtime
remains source `1bf6a2d7` with xlang3 `7b8b32ae`; the previous 98-contract native
gate and packaged smoke retain their original scope. No native source changed
and no native/model test was rerun for this input-adapter change.

Local artifact:
`D:\CantorAI2026\xMind\.agentflow\ci\editor-selection-release-vsix\xmind-0.1.3.vsix`

The installed version 0.1.1 and pending migration target were not replaced.
Read-only UI observation confirms that **Stop and migrate** still awaits the
user's answer. Installed/rendered selection, real coding approval interaction
and full pinned product parity remain unverified.
