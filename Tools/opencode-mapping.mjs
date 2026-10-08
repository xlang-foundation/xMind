// Source-level equivalents only. These entries do not assert passing parity.
// The Python prototype was removed. Never map deleted sources as capabilities.
export const partial = {};

const requirements = {
  session: 'Persist the full session lifecycle, typed messages, input ordering and configuration; validate restart, concurrent inputs, cross-client consistency and missing/terminal session errors.',
  permission: 'Persist and enforce explicit scoped decisions consistently across local tools, remote tools and clients; validate denial, cancellation, replay, races and saved-rule revocation.',
  fs: 'Validate workspace boundaries, path encoding, symlinks, binary/large files, errors and permission enforcement using real temporary repositories.',
  model: 'Validate model discovery/default selection and per-model capabilities using the pinned LiteLLM reference, configured deployments and actual provider contracts on xlang3.',
  provider: 'Validate provider discovery, configuration, supported models and authentication on xlang3; credentials must not appear in client responses or events.',
  integration: 'Validate key, OAuth and command connection lifecycles, polling, cancellation, expiry and failure without exposing credentials.',
  credential: 'Validate credential storage, update, activation and removal with secret redaction, correct provider scoping and failure recovery.',
  mcp: 'Interoperate with independent peers for discovery, invocation and resources; validate configured transports, authorization, cancellation and remote permission enforcement.',
  pty: 'Validate real terminal creation, resizing, input/output, reconnect, token expiry and cleanup on Windows; identify native gaps before changing runtime code.',
  shell: 'Validate real command execution, working directory/environment, output cursors, cancellation, exit status and permission enforcement on Windows.',
  vcs: 'Compare real Git repositories for branches, status, diffs and review base; validate dirty files, renames, binary changes and error cases.',
  worktree: 'Validate real Git worktree creation, discovery, refresh and safe removal with preservation of user changes and recorded session associations.',
  skill: 'Validate discovery, metadata, instruction loading and activation with precedence, workspace scoping and shared CLI/editor behavior.',
  agent: 'Validate reproducible agent definitions, lookup and switching with instructions, tools, models and graph reuse.',
  command: 'Validate command discovery and invocation with arguments, agent selection and durable conversation effects.',
  plugin: 'Validate plugin discovery, lifecycle, update checks, hooks and failures without silent loss of registered capabilities.',
  rpc: 'Validate plugin RPC registration, dispatch, typed payloads, errors and authorization boundaries.',
  config: 'Validate effective configuration, precedence, shell discovery, secret redaction and consistent reload across clients.',
  location: 'Validate location/workspace discovery, configuration reload and isolation of sessions, tools and events.',
  project: 'Validate project discovery and metadata updates on real repositories with persistent identifiers and client consistency.',
  reference: 'Validate reference discovery, typed content and attachment resolution against actual workspace/provider sources.',
  form: 'Validate durable pending human-input forms, reply/cancellation, ownership, races and restart recovery.',
  websearch: 'Validate provider configuration and actual search results, network failure/cancellation and credential handling.',
  info: 'Validate server identity, version and advertised capabilities against the running backend.',
  debug: 'Validate location inspection/eviction with safe cleanup and no corruption of ongoing work.',
  experimental: 'Inspect the pinned implementation for this experimental operation; validate its exact lifecycle and failures rather than infer semantics from its name.',
};

export function family(id, path) {
  if (path.includes('persistent-pty') || path.endsWith('/terminal') || path.endsWith('/terminal/read')) return 'pty';
  if (path.includes('/permission')) return 'permission';
  if (path.includes('/instructions') || path.endsWith('/skill')) return 'skill';
  if (path.includes('/experimental/mcp')) return 'mcp';
  if (path.includes('/experimental/integration')) return 'integration';
  if (path.includes('/experimental/fs')) return 'fs';
  if (path.includes('/experimental/config')) return 'config';
  if (path.includes('/experimental/session')) return 'session';
  if (id === 'event.subscribe') return 'session';
  const prefix = id.split('.')[0];
  if (prefix === 'server') return 'info';
  return prefix;
}

export function acceptance(id, path) {
  const group = family(id, path);
  if (!requirements[group]) throw new Error(`Unmapped operation family: ${id}`);
  return { family: group, behavior: requirements[group],
    contract: 'Compare the pinned request parameters/body, response schemas/statuses and source behavior with the AgentFlow equivalent. Different API paths are allowed; observable capability parity is required.',
    evidence: 'Run the operation-specific success and failure scenarios through the actual xlang3 backend, then verify corresponding CLI/editor workflows where exposed. Record commands/results; source presence alone is insufficient.' };
}
