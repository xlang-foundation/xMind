// Reviewed native candidates only. Matching source is not observable parity.
const http = 'Native/src/http_server.cpp';
const workspace = 'Native/src/workspace_tools.cpp';
export const nativePartial = {
  'session.list': [http, 'GET /v1/sessions', '"/v1/sessions"', 'Location/project scoping, filters and pagination remain to be compared.'],
  'session.create': [http, 'POST /v1/sessions', 'persistence.create_session', 'The upstream session fields, agent/environment configuration and full creation lifecycle remain incomplete.'],
  'session.update': [http, 'POST /v1/sessions/{id}/title', 'persistence.rename_session', 'Only optimistic title updates exist; other upstream session metadata changes remain incomplete.'],
  'session.message.list': [http, 'GET /v1/sessions/{id}/history', 'persistence.history', 'Durable role/data history exists; upstream message IDs, typed parts and pagination need comparison.'],
  'session.prompt': [http, 'POST /v1/runs', 'executor->submit_model', 'Native model/tool admission exists; attachments, queued input and upstream session input ordering remain incomplete.'],
  'session.interrupt': [http, 'POST /v1/runs/{id}/cancel', 'executor->cancel', 'Run cancellation exists; session-level interruption and resumable upstream semantics remain incomplete.'],
  'session.switchModel': [http, 'Native per-run model override', 'executor->submit_model', 'A per-run override is not persistent session model switching; upstream behavior remains incomplete.'],
  'session.permission.list': [http, 'GET /v1/runs/{id}/operations', 'persistence.operations', 'Run-scoped effect proposals exist; full session permission categories and lifecycle remain incomplete.'],
  'session.permission.reply': [http, 'POST /v1/operations/{id}/decision', 'persistence.decide_operation', 'Single-use allow/deny exists; upstream saved rules and permission category behavior remain incomplete.'],
  'permission.request.list': [http, 'GET /v1/runs/{id}/operations', 'persistence.operations', 'Only run-scoped operations exist; a global/location pending permission catalogue is absent.'],
  'event.subscribe': ['Native/src/http_server.cpp', 'Native per-run durable event observation', 'persistence.events', 'Per-run event reads support client polling; upstream global/location subscription semantics remain incomplete.'],
  'session.log': [http, 'GET /v1/runs/{id}/events', 'persistence.events', 'Run events are not a complete upstream session log with its filtering and response contract.'],
  'fs.find': [workspace, 'Native search_files tool', '"search_files"', 'Literal content search exists; the upstream filename search API and navigation behavior are absent.'],
  'fs.list': [workspace, 'Native list_files tool', '"list_files"', 'Bounded workspace directory listing exists; upstream filesystem API response and client navigation remain incomplete.'],
  'fs.read': [workspace, 'Native read_file tool', '"read_file"', 'Bounded UTF-8 reads exist; binary/range semantics and the upstream filesystem API remain incomplete.'],
  'experimental.fs.write': ['Native/src/agent_runner.cpp', 'Reviewed native edit_file/create_file tools', 'CreateExecutor', 'Approved exact edits and creation exist; general upstream filesystem write API and its full semantics remain incomplete.'],
  'model.list': [http, 'GET /v1/models; POST /v1/provider/models', '"/v1/models"', 'Enabled models and OpenAI account discovery exist; broad provider capabilities and upstream catalogue fields remain incomplete.'],
  'model.default': ['Native/src/provider_setup.cpp', 'Selected backend provider model metadata', 'ProviderRuntime::configuration', 'A configured backend model exists; the upstream default selection API and per-location defaults remain incomplete.'],
  'provider.list': [http, 'GET /v1/provider/configuration', '"/v1/provider/configuration"', 'Single OpenAI-compatible configuration exists; a multi-provider registry is absent.'],
  'provider.get': [http, 'GET /v1/provider/configuration', '"/v1/provider/configuration"', 'Single configured provider metadata exists; provider-by-ID contracts and broader native providers remain incomplete.'],
  'experimental.config.update': [http, 'POST /v1/provider/configuration', '"/v1/provider/configuration"', 'Optimistic provider enrollment exists; general effective configuration precedence and reload remain incomplete.'],
  'mcp.list': [http, 'GET /v1/mcp/servers', '"/v1/mcp/servers"', 'Saved stdio server metadata exists; upstream connection lifecycle fields, HTTP/OAuth and live model/tool acceptance remain incomplete.'],
  'server.info': [http, 'GET /v1/health', '"/v1/health"', 'Native health/capabilities exist; the full pinned identity/version response remains incomplete.'],
};
