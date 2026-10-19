# Native language-server integration

xMind starts configured language servers inside the native backend and exposes
their read-only queries to AgentRunner as one `language_server` tool. The model
can select only a registered server, a workspace-relative file, and one of
`hover`, `definition`, `references`, `documentSymbol`, or `diagnostics`. It
cannot choose the executable, arguments, environment, or a path outside the
active workspace. Language servers cannot apply edits through this client.

## Diagnostics source update — 2026-10-19

The new native source negotiates pull diagnostics through the server's
`diagnosticProvider` capability. A push-only server instead supplies
`textDocument/publishDiagnostics`; xMind advertises version support, waits for
an actual publication within the configured request deadline, and clears the
cached publication when the opened buffer changes. Explicitly mismatched
document versions are ignored. Unversioned publications remain server-reported
snapshots, with no claim that they prove all project analysis has finished.
Missing publications and failed pull requests are errors, never invented clean
results or a fallback to a cached empty publication.

Notifications bind to an already opened document after decoding file-URI
escapes and normalizing its Windows drive letter. This handles TypeScript's
`file:///c%3A/...` response to a `file:///C:/...` request without opening a new
path. Path spelling beyond the drive letter is preserved.

Actual TypeScript language server 5.3.0 and compiler 6.0.3 validate the native
HTTP path: TS2322 on disk, clean diagnostics for an unsaved correction, the
disk error again, then clean diagnostics after saving the fixture correction.
The compiler exits 2 before and 0 after. This is a real language-server/compiler
check with harness-authored fixture changes, not a model-driven repair loop or
VS Code pane acceptance. Five related native contracts pass. The initial Rust
leaf fails call-site navigation; a separate logged diagnostic passes without
product changes. That startup failure remains unexplained.

The complete current local native suite now passes **153/153**. A separate
actual Claude coding task observes TS2322 and compiler exit 2, makes one
reviewed initializer edit, and confirms clean diagnostics/compiler exit 0.
An independent compiler check also passes; the recorded run has 13 messages
and two diagnostic queries. This closes that specific model-driven repair-loop
check. It does not close the earlier intermittent Rust startup failure.
[Complete native qualification](evidence/lsp-diagnostics-full-native-20261019.json),
[actual coding-agent repair](evidence/live-typescript-diagnostics-repair-20261019.json).

This update is now in **installed 0.1.40**. The installed CLI and browser agent
both execute real TypeScript diagnostics; the CLI also runs the actual compiler
successfully. Hosted-native and actual editor-pane acceptance remain pending.
Older browser/backend instances remain preserved.
[Installed runtime evidence](evidence/coding-package-0.1.40-20261019.json).
[Source, baseline failures and actual integration evidence](evidence/native-lsp-push-diagnostics-20261019.json).
Protocol references: [LSP specification](https://microsoft.github.io/language-server-protocol/specifications/lsp/3.17/specification/),
[TypeScript language server](https://github.com/typescript-language-server/typescript-language-server).

Register servers from a trusted JSON or YAML file at backend startup. For
example:

```yaml
profiles:
  - id: rust-analyzer
    executable: C:/tools/rust-analyzer.exe
    language_id: rust
    extensions: [.rs]
    arguments: []
    request_timeout_ms: 30000
```

`executable` must be an absolute path to an existing file. `extensions` must
list one or more file suffixes. Optional `environment` values and `arguments`
are trusted owner configuration, never model input. The runtime records the
profile in its local SQLite store, assigns a revision, and binds the executable
identity and environment hashes into AgentFlow authority. Re-importing the same
profile is idempotent; changing or retiring it advances the revision. Changing
the startup configuration of an existing managed local profile requires an
explicit backend reconfiguration rather than silently replacing its authority.

Use `xmind serve --language-server-config FILE` for a directly managed server,
or `xmind --workspace DIR --language-server-config FILE chat` to configure a
new local workspace profile. `xmind view --workspace DIR --language-server-config
FILE --ready-file FILE` passes the same trusted profile into the local browser
backend. Local profile settings are pinned when that workspace backend is
created. VS Code's managed backend accepts the same file through the machine
setting `agentflow.languageServerConfigPath`; a development host may use the
repository `.config/language-servers.yaml` automatically when that file exists.
The extension passes the path to xMind without reading the configuration.

The real Rust Analyzer contract verifies persistent workspace initialization,
repeated document symbols, clean pull diagnostics, user-triggered document
formatting through both the standalone and provider-profile runtime, and profile
import/revision behavior against SQLite. The VS Code extension publishes those
diagnostics and registers definition, references,
hover and document-formatting providers for configured
languages. Provider requests use the unsaved editor buffer and workspace
authority; returned locations outside the opened folder are dropped. Automated
extension contracts cover request mapping, hover rendering, workspace bounds
and stale/cancelled replies. Semantic definition/reference/hover behavior has
passed against a real Rust Analyzer call-site fixture. Native authenticated
HTTP and the authorized browser-view path also return its definition locations.
The client retries up to four times with a short bounded delay for the LSP
`RequestCancelled`, `ContentModified`, and `ServerCancelled` responses while
project analysis changes. Formatting uses VS Code's current unsaved
buffer and indentation options, checks returned edits against the document,
and applies them through the editor formatter API. A rendered VS Code Extension
Development Host formatted and saved a Rust fixture through the configured
provider-profile backend; the saved source also passed `rustfmt --check`.
AgentRunner's model-visible language-server tool stays read-only. Packaged
extension acceptance, formatter discovery/configuration and a rendered editor
edit/build/diagnose repair loop remain open. The native CLI TypeScript loop above
has its own actual-model acceptance. See [validation status](VALIDATION_STATUS.md) and
[the 2026-10-11 formatting contract](evidence/native-language-server-formatting-local-20261011.json).

## Multi-folder editor routing

An authenticated `/v1/language-server/query` may include a registered
`folder_id`. Native checks that ID against a fresh workspace-membership snapshot,
opens that member's root, and keeps its language-server process separate from
the primary root's process. The query path remains relative to the selected
root. Responses echo the selected ID, and removed or unregistered folders are
rejected before dispatch; cached servers for removed members are retired.

VS Code loads the registered membership and maps each open file to exactly one
member root. Diagnostics, navigation and formatting for attached folders carry
that member's ID. A document outside every registered folder receives no
language-server result. The native multi-folder contract uses real Rust Analyzer
projects with identical `src/lib.rs` paths and proves each query resolves the
symbol from its own root, then rejects unknown and removed IDs. See
[multi-folder language-server evidence](evidence/native-multifolder-lsp-local-20261011.json).

AgentRunner's model-visible `language_server` tool accepts the optional
`folder_id` from the run's admitted workspace snapshot. Without it, queries use
the primary folder. The native dispatcher routes each selected member to its
own cached language-server registry and does not consult live membership during
the run. The AgentRunner contract now drives the complete native tool loop with
a synthetic provider peer and real Rust Analyzer: it verifies the secondary
symbol result, adds a third folder during the run, and confirms it is rejected
by the pinned snapshot. A second AgentRunner run then uses revision 3 and returns
the new folder's distinct Rust symbol. A live-model turn and rendered
multi-folder VS Code session remain open.
