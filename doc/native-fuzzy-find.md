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
browser adapter forwards this as a read-only route through its origin-bound
view credential and rejects duplicate or malformed parameters before
forwarding. This provides the backend and protocol needed by browser and VS
Code clients; a dedicated file-finder control is not yet rendered in either
client.

The Release `native_workspace_tools_contract` verifies real filesystem
fixtures for fuzzy ranking, file/directory filtering, hidden and ignored paths,
result truncation and the Agent tool JSON. `native_agent_http_contract` checks
the authenticated API route, malformed-query rejection and view-session access.
`views/browser/tests/server.test.cjs` passes 10/10, including the browser
adapter’s forwarded read request and query rejection. Agent, graph, delegation,
dynamic-plan, process, edit and Responses HTTP contracts were also rerun during
this change; results are recorded in the current task run.

This is a reviewed source candidate for pinned OpenCode `fs.find`, not verified
operation parity. The inventory retains the `missing` parity status until the
exact `location` semantics, ranking order, response contract and client
navigation are compared and accepted. The implementation uses a bounded
subsequence ranker; it does not claim OpenCode’s exact ranking equivalence.
