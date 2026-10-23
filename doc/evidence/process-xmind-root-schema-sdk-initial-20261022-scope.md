# Initial schema SDK integration failure

The complete build passed, followed by 8/12 focused contracts in 71.58 seconds. The schema, MCP effect/input and model RPC contracts failed. Windows text stdout produced CRLF for the endpoint bootstrap; the parent and direct SDK fixture removed LF only, leaving CR in the endpoint. The parent then rejected the port. The MCP effect fixture recorded no created operation or effect marker. Original patches, all logs and input/binary hashes are retained; failed fixtures are not represented as accepted validation.

The correction accepts the endpoint's single optional CR before LF and retains exact numeric port validation. No method-call JSON-RPC dispatcher was restored, no deadline or resource limit changed, and no user task was replayed. A fresh focused gate is running; this record does not claim its outcome.
