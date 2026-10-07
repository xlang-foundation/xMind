// Inventory the pinned source API without executing OpenCode or installing it.
import { readFileSync, writeFileSync } from 'node:fs';
import { resolve } from 'node:path';
import { execFileSync } from 'node:child_process';
import { createHash } from 'node:crypto';
import { partial, acceptance } from './opencode-mapping.mjs';

const root = resolve(import.meta.dirname, '..');
const reference = resolve(root, '.agentflow/reference/opencode');
const expected = '3a103fe0aff726a4edc7492f03f7b88195d9e4c9';
const revision = execFileSync('git', ['-C', reference, 'rev-parse', 'HEAD'], { encoding: 'utf8' }).trim();
if (revision !== expected) throw new Error('Reference revision differs from pinned OpenCode v2.0.16');
const schemaBytes = readFileSync(resolve(reference, 'packages/protocol/openapi.json'));
const schema = JSON.parse(schemaBytes.toString('utf8'));
const operations = [];
for (const [path, methods] of Object.entries(schema.paths)) {
  for (const [method, operation] of Object.entries(methods)) {
    if (!['get', 'post', 'put', 'patch', 'delete', 'head', 'options'].includes(method)) continue;
    const id = operation.operationId;
    if (!id) throw new Error(`Operation ID missing: ${method} ${path}`);
    const mapping = partial[id];
    if (mapping) readFileSync(resolve(root, mapping[0]));
    operations.push({ method: method.toUpperCase(), path, operationId: id,
      summary: operation.summary ?? '', tags: operation.tags ?? [],
      status: mapping ? 'partial-source-unverified' : 'missing',
      agentflowEvidence: mapping ? { source: mapping[0], surface: mapping[1], gap: mapping[2], verifiedParity: false } : null,
      upstreamContract: { parameters: [...(methods.parameters ?? []), ...(operation.parameters ?? [])],
        requestBody: operation.requestBody ?? null, responses: operation.responses ?? {} },
      acceptanceCriteria: acceptance(id, path) });
  }
}
operations.sort((a, b) => a.path.localeCompare(b.path) || a.method.localeCompare(b.method));
const inventory = { source: 'https://github.com/anomalyco/opencode/tree/v2.0.16', revision,
  schema: 'packages/protocol/openapi.json', schemaSha256: createHash('sha256').update(schemaBytes).digest('hex'),
  schemaReferences: 'Resolve $ref values against the pinned source schema. This inventory does not dereference them or cover every non-API behavior.',
  operationCount: operations.length, operations };
const target = resolve(root, 'Documents/OPENCODE_API_INVENTORY.json');
writeFileSync(target, JSON.stringify(inventory, null, 2) + '\n');
console.log(`Mapped ${operations.length} pinned OpenCode operations: ${operations.filter(x => x.agentflowEvidence).length} partial source equivalents; no verified parity claims`);
