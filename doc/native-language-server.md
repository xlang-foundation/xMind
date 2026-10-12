# Native language-server integration

xMind starts configured language servers inside the native backend and exposes
their read-only queries to AgentRunner as one `language_server` tool. The model
can select only a registered server, a workspace-relative file, and one of
`hover`, `definition`, `references`, `documentSymbol`, or `diagnostics`. It
cannot choose the executable, arguments, environment, or a path outside the
active workspace. Language servers cannot apply edits through this client.

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
extension acceptance, formatter discovery/configuration and a complete
edit/build/diagnose repair loop remain open; see [validation status](VALIDATION_STATUS.md) and
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

AgentRunner's model-visible `language_server` tool still uses the primary root;
propagating the admitted multi-folder snapshot into model tool calls remains
open.
