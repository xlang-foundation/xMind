# Native MCP tool nodes in graphs

A registered graph can invoke a configured MCP tool directly, without a model
or provider key. Its child uses the same native `McpToolRegistry`, controller
approval and durable operation journal as an agent tool call. The backend owns
the peer process, arguments, schema evaluation, cancellation and result; views
read the common graph/run/operation protocol.

## Configure and discover

While xMind Server is stopped, import the trusted stdio server configuration
and provision any referenced credentials as described in [native MCP](native-mcp.md).
Then use the compiled native administrator to discover its actual catalogue:

```powershell
.\build\native\Release\xmind_admin.exe --db STATE_DB --modules XLANG3_MODULES --stdlib PURE_STDLIB discover-mcp SERVER_ID WORKSPACE
```

The command acquires the normal database ownership lease, verifies the
workspace, connects only the named enabled server and validates its complete
tool catalogue with a 30-second deadline. It returns server/configuration
revision, negotiated protocol and public tool aliases, descriptions and input
schema JSON. Discovery never sends `tools/call` or grants approval. Starting a
trusted peer process can have its own effects; the MCP journal tracks tool
dispatch. Configured credentials remain privately resolved and cleared, and
reflection in public descriptions/schema strings, keys or raw schema text
rejects discovery with a generic diagnostic. No executable path or secret is
returned by this command.

Copy the returned alias and configuration revision into the trusted graph
catalogue. The following illustrates the node fields; the alias placeholder
must be replaced with the actual discovered value:

```json
{
  "id": "external",
  "type": "tool",
  "tool": "<alias returned by discover-mcp>",
  "mcp": {"server_id": "project-tools", "config_revision": 1},
  "arguments_json": "{\"path\":\"README.md\"}"
}
```

MCP aliases are `mcp_` followed by 48 lowercase hexadecimal characters. They
bind the configured server ID/revision and the fingerprint of the actual peer
tool name, description, input/output schemas and annotations. A raw peer tool
name cannot select a graph tool. Import the catalogue with `import-graphs`;
the ordinary CLI `graphs`, `graph-run`, `graph-watch`, `graph-input`, `decide`
and history commands, and the existing view adapter, use the same backend.

## Arguments and execution

MCP nodes exclusively use an `arguments_json` string containing a strict JSON
object of at most 65,536 bytes. Built-in tool nodes continue using `arguments`
objects. Storing literal JSON inside a string retains its original number
tokens, escaped property names and whitespace through catalogue/checkpoint
serialization and SQLite close/reopen. Resolved arguments still undergo the
registry's strict schema validation and whitespace compaction before approval
and dispatch, which preserves number tokens and string escapes.

The ordinary dependency `$ref` form works inside that string. References must
name declared dependencies and use the persisted typed graph output. Strings,
booleans, null, safe integers, arrays and objects are accepted. For MCP
references, floating-point values or integers outside ±9,007,199,254,740,991,
including values nested in containers, reject before child admission. Original
arbitrary numeric tokens in dependency output are not claimed preserved by the
legacy typed graph representation. Literal decimals/large integers in the
argument string remain exact; this restriction applies to substituted values.

Metadata and admission only check the verified workspace and immutable enabled
server/revision binding. They never start a peer. Execution creates one fresh
owned client for the pinned server and rediscovery must contain the exact alias
before an operation can be proposed. Descriptor drift fails without a
`tools/call`. Independent children do not share single-caller clients.
The root segment deadline and cancellation cover launch, discovery, schema
evaluation, approval waiting and dispatch. Received acknowledgements retain the
registry's separately bounded output validation/journaling budget.

Every call needs an exact controller decision even if its untrusted
`readOnlyHint` says otherwise. The durable proposal binds workspace, configured
server/revision, alias/catalogue fingerprint, schemas, protocol and exact
arguments. Successful child output retains the actual operation/request IDs
and escaped peer response, labelled `acknowledged_by_peer:true` and
`independently_verified:false`. A dependent node consumes that observed output;
no model response or usage is invented for an MCP-only graph.

## Failure and recovery

Denial/cancellation before dispatch retires the proposal without a tool call.
Lost replies after an actual effect retain an uncertain operation and fail the
graph; no retry or execution replay occurs. Its quarantine can block a new,
separately approved root. An acknowledged output larger than the final 64-KiB
graph envelope fails the child explicitly while retaining the succeeded MCP
operation and response in its journal. It does not emit `tool.completed` or
claim the external effect was undone.

If an actual acknowledgement cannot be journaled, `McpOutcomeUnrecorded`
propagates to the graph service's fail-stop ownership boundary. Later admission
and queued dispatch stop; restart quarantines unfinished claims instead of
fabricating retirement or repeating the effect.

An already committed human pause with a removed, disabled or changed MCP
revision remains inspectable and cancellable after reopening. Human input
validates its immutable binding before any checkpoint mutation or scheduling.
It rejects a stale binding and does not silently rebind to current settings.
New admission and executable catalogue metadata reject that stale binding too.

## Verification scope

The final frozen source passed all **69 native contracts in 121.22 seconds**,
with the exact expected/registered/passed manifest, zero failures/skips and no
post-build exclusions. The new graph contract passed in **5.59 seconds** and
HTTP/CLI contract in **2.31 seconds**. All 14 changed native/config/fixture
source hashes match the capture before the final guarded build. Fresh
browser/native integration passed the rebuilt server and source-matched assets.
Frontend sources are unchanged from the exact preceding hosted `fceb50ba`
gate and retain its **98 extension and 17 browser tests** in the local scope,
without rerunning those suites locally for this native change.
[Local evidence](evidence/native-graph-mcp-local-provenance.json).

Exact revision `c4ec09fc3ee25c1b3a2bd9087b25f5af420ee616` subsequently passed
the [hosted gate](https://github.com/xlang-foundation/xMind/actions/runs/37755382937):
**69 native contracts in 184.36 seconds**, **98 extension tests in 3.2107402
seconds** and **17 browser tests in 1.5543002 seconds**, with zero failures/skips,
the exact registered/expected/passed manifest and all 16 job steps successful.
The new graph and HTTP/CLI contracts took **12.60/3.87 seconds**. Hosted
native/browser integration and verification of the 18 required VSIX assets
passed. All 14 final local source hashes match that exact hosted revision.
[Hosted evidence](evidence/native-graph-mcp-hosted-provenance.json).

The exact evidence/runtime archives and contained VSIX were downloaded and
verified against GitHub artifact digests, source/runtime/standard-library pins
and the SQLite prerequisite patch hash. Every destination was validated inside
fresh owned CI folders before extraction. The provenance records all **29
runtime files, 30 VSIX files and 12 browser-runtime files** with SHA-256 hashes;
direct packaged source text matches the exact revision modulo checkout CRLF.
Original job-log bytes retain their UTF-8 BOM. This artifact verification does
not execute/install the bundle or establish an installed-preview upgrade.

The first intermediate gate failed one HTTP startup case because the existing
workspace guard demanded a model capability flag without a model. The final
guard permits model-free graph startup and retains explicit supported tool
capability checks when a model is configured. Missing, unknown and unsupported
configured-model declarations reject before startup. The complete frozen gate
was rerun after correction; the [initial failure](evidence/native-graph-mcp-initial-ctest.log)
is retained separately and is not a pass claim.

The new
`native_graph_mcp_contract` exercises actual native graph/permission engines,
owned Node subprocesses, real disposable file effects and embedded-xlang3
SQLite. `native_graph_mcp_http_contract` exercises actual native administrator,
server, CLI and the shared view adapter. Their peer metadata/protocol replies
are explicitly synthetic. The scope does not establish live providers,
rendered IDE acceptance, dynamic graph planning, MCP HTTP/OAuth/resources,
outbound A2A or complete coding/protocol parity. Installed-preview verification
is a separate scope. The separate
[preceding hosted67 evidence](evidence/native-anthropic-history-hosted-provenance.json)
covers Claude history source and excludes this newer MCP graph implementation.
