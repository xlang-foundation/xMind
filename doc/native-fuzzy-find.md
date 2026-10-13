# Native fuzzy workspace file discovery

The native workspace registry now exposes `find_files` to single agents, graph
agents, read-only inspection/delegation scopes and the workspace HTTP view. The
C++ implementation uses the existing handle-verified, bounded workspace
traversal. It ranks case-insensitive fuzzy path matches and supports file-only
or directory-only results, a selected subdirectory, result limits, hidden-file
opt-in and repository ignore rules. Links and private backend state remain
outside the search boundary; traversal truncation is explicit.

The authenticated native route is `GET /v1/workspace/find` with `query`,
optional `path`, `type`, `limit`, `hidden` and `respect_ignore` parameters. The
browser and VS Code views now render a filename finder above the workspace
tree. Search spans all registered folders; selecting a result opens that exact
folder's read-only preview. The local view validates folder membership and
native identity before searching attached folders. The browser adapter
forwards the finder as a read-only route through its origin-bound view
credential and rejects duplicate or malformed parameters before forwarding.

The Release `native_workspace_tools_contract` verifies real filesystem
fixtures for fuzzy ranking, file/directory filtering, hidden and ignored paths,
result truncation and the Agent tool JSON. `native_agent_http_contract` checks
the authenticated API route, malformed-query rejection and view-session access.
`views/browser/tests/connection.test.cjs` verifies single-root search,
multi-folder search and opening the selected result. `views/browser/tests/server.test.cjs`
verifies credential forwarding and malformed-query rejection.
`native_local_view_contract` exercises both registered folders through the real
xMind local view and rejects an unregistered folder. The browser test suite
passes 102/102 and the VS Code extension test suite passes 321/321.

This is a reviewed source candidate for pinned OpenCode `fs.find`, not verified
operation parity. The inventory retains the `missing` parity status until the
exact `location` semantics, ranking order, response contract and client
navigation are compared and accepted. The implementation uses a bounded
subsequence ranker; it does not claim OpenCode’s exact ranking equivalence.
