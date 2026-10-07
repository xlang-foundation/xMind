// Audit downloaded, pinned LiteLLM metadata without importing its SDK.
import { readFileSync, writeFileSync } from 'node:fs';
import { resolve } from 'node:path';
import { createHash } from 'node:crypto';

const root = resolve(import.meta.dirname, '..');
const reference = resolve(root, '.agentflow/reference/litellm');
const revision = 'e98bbc2f8e035a382044524c73341d8b5800d492';
if (readFileSync(resolve(reference, 'REVISION'), 'utf8').trim() !== revision)
  throw new Error('LiteLLM reference revision differs from the pinned audit');
const raw = readFileSync(resolve(reference, 'model_prices_and_context_window.json'));
const catalogue = JSON.parse(raw.toString('utf8'));
const groups = new Map();
let models = 0;
const metadataEntries = [];
const unspecifiedModeEntries = [];
for (const [id, record] of Object.entries(catalogue)) {
  if (id === 'sample_spec') continue;
  if (!record || typeof record !== 'object') throw new Error(`Invalid catalogue entry: ${id}`);
  if (typeof record.litellm_provider !== 'string') {
    metadataEntries.push(id);
    continue;
  }
  if (!record.mode) unspecifiedModeEntries.push(id);
  models++;
  const provider = record.litellm_provider;
  if (!groups.has(provider)) groups.set(provider, { provider, modelEntries: 0, modes: {},
    declaredCapabilities: {}, agentflowStatus: 'unverified',
    acceptance: 'Validate provider authentication, model identifiers, streaming, cancellation, usage, errors and each requested model capability on xlang3.' });
  const group = groups.get(provider);
  group.modelEntries++;
  const mode = record.mode ?? 'unspecified';
  group.modes[mode] = (group.modes[mode] ?? 0) + 1;
  for (const [key, value] of Object.entries(record)) {
    if (key.startsWith('supports_') && value === true)
      group.declaredCapabilities[key] = (group.declaredCapabilities[key] ?? 0) + 1;
  }
}
const inventory = { source: `https://github.com/BerriAI/litellm/tree/${revision}`, revision,
  sdkVersion: '1.106.0', cataloguePath: 'model_prices_and_context_window.json',
  catalogueSha256: createHash('sha256').update(raw).digest('hex'),
  modelEntries: models, providerGroups: groups.size,
  metadataEntries, unspecifiedModeEntries,
  caveat: 'Pinned upstream metadata is a coverage reference, not tested AgentFlow support, current availability or current pricing. Entries include aliases, regions, pricing buckets and non-chat modes. Missing modes remain unspecified; non-provider metadata is excluded and recorded separately.',
  providers: [...groups.values()].sort((a, b) => a.provider.localeCompare(b.provider)) };
writeFileSync(resolve(root, 'Documents/LITELLM_PROVIDER_INVENTORY.json'), JSON.stringify(inventory, null, 2) + '\n');
console.log(`Audited ${models} model entries across ${groups.size} provider groups; AgentFlow support remains unverified`);
