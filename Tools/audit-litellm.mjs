// Audit downloaded, pinned LiteLLM metadata without importing its SDK.
import { readFileSync, writeFileSync } from 'node:fs';
import { resolve } from 'node:path';
import { createHash } from 'node:crypto';
import { nativePartial } from './litellm-native-mapping.mjs';

const root = resolve(import.meta.dirname, '..');
const reference = resolve(root, '.agentflow/reference/litellm');
const revision = 'e98bbc2f8e035a382044524c73341d8b5800d492';
if (readFileSync(resolve(reference, 'REVISION'), 'utf8').trim() !== revision)
  throw new Error('LiteLLM reference revision differs from the pinned audit');
const raw = readFileSync(resolve(reference, 'model_prices_and_context_window.json'));
const catalogueSha256 = createHash('sha256').update(raw).digest('hex');
if (catalogueSha256 !== '17bce2d7987ff08ea3a80b03ce8a2a9529dfed9d2595097993ad63454aa5369e')
  throw new Error('LiteLLM catalogue bytes differ from the pinned reference');
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
    acceptance: 'Validate native C++ provider authentication, model identifiers, streaming, cancellation, usage, errors and each requested model capability. xlang3 remains the embedded script/database runtime.' });
  const group = groups.get(provider);
  group.modelEntries++;
  const mode = record.mode ?? 'unspecified';
  group.modes[mode] = (group.modes[mode] ?? 0) + 1;
  for (const [key, value] of Object.entries(record)) {
    if (key.startsWith('supports_') && value === true)
      group.declaredCapabilities[key] = (group.declaredCapabilities[key] ?? 0) + 1;
  }
}
for (const [provider, mapping] of Object.entries(nativePartial)) {
  const group = groups.get(provider);
  if (!group) throw new Error(`Native mapping has no pinned provider group: ${provider}`);
  group.nativeStatus = 'partial';
  group.nativeEvidence = {
    sources: mapping.sources.map(([path, anchor]) => {
      const source = readFileSync(resolve(root, path), 'utf8').replace(/\r\n/g, '\n');
      if (!source.includes(anchor)) throw new Error(`Native provider anchor absent: ${path}`);
      return { path, anchor, sha256: createHash('sha256').update(source).digest('hex') };
    }),
    artifacts: mapping.evidenceFiles.map(path => {
      const source = readFileSync(resolve(root, path), 'utf8').replace(/\r\n/g, '\n');
      JSON.parse(source);
      return { path, sha256: createHash('sha256').update(source).digest('hex') };
    }),
    scope: mapping.scope, gaps: mapping.gaps,
  };
}
for (const group of groups.values()) if (!group.nativeStatus) group.nativeStatus = 'unmapped';
const inventory = { source: `https://github.com/BerriAI/litellm/tree/${revision}`, revision,
  sdkVersion: '1.106.0', cataloguePath: 'model_prices_and_context_window.json',
  catalogueSha256,
  modelEntries: models, providerGroups: groups.size,
  metadataEntries, unspecifiedModeEntries,
  nativeCoverage: { partialProviderGroups: Object.keys(nativePartial).length,
    unmappedProviderGroups: groups.size - Object.keys(nativePartial).length,
    verifiedProviderParity: 0,
    caveat: 'Source anchors and selected recorded live checks are partial evidence. No complete provider-group or model-entry parity is verified.' },
  caveat: 'Pinned upstream metadata is a coverage reference, not tested AgentFlow support, current availability or current pricing. Entries include aliases, regions, pricing buckets and non-chat modes. Missing modes remain unspecified; non-provider metadata is excluded and recorded separately.',
  providers: [...groups.values()].sort((a, b) => a.provider.localeCompare(b.provider)) };
writeFileSync(resolve(root, 'Documents/LITELLM_PROVIDER_INVENTORY.json'), JSON.stringify(inventory, null, 2) + '\n');
console.log(`Audited ${models} entries across ${groups.size} provider groups; ${Object.keys(nativePartial).length} native partial candidates, ${groups.size - Object.keys(nativePartial).length} unmapped, 0 verified provider parity`);
