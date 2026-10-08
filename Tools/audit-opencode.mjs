// Inventory the pinned source API without executing OpenCode or installing it.
import { readFileSync, writeFileSync } from 'node:fs';
import { resolve } from 'node:path';
import { execFileSync } from 'node:child_process';
import { createHash } from 'node:crypto';
import { partial, acceptance } from './opencode-mapping.mjs';
import { nativePartial } from './opencode-native-mapping.mjs';

const root = resolve(import.meta.dirname, '..');
const reference = resolve(root, '.agentflow/reference/opencode');
const expected = '3a103fe0aff726a4edc7492f03f7b88195d9e4c9';
const revision = execFileSync('git', ['-C', reference, 'rev-parse', 'HEAD'], { encoding: 'utf8' }).trim();
if (revision !== expected) throw new Error('Reference revision differs from pinned OpenCode v2.0.16');
// Read the committed object so a dirty reference checkout cannot alter the pin.
const schemaBytes = execFileSync('git', ['-C', reference, 'show', expected + ':packages/protocol/openapi.json'], { maxBuffer: 16 * 1024 * 1024 });
const schema = JSON.parse(schemaBytes.toString('utf8'));
const operations = [];
for (const [path, methods] of Object.entries(schema.paths)) {
  for (const [method, operation] of Object.entries(methods)) {
    if (!['get', 'post', 'put', 'patch', 'delete', 'head', 'options'].includes(method)) continue;
    const id = operation.operationId;
    if (!id) throw new Error(`Operation ID missing: ${method} ${path}`);
    const mapping = partial[id];
    if (mapping) readFileSync(resolve(root, mapping[0]));
    const native = nativePartial[id];
    let nativeEvidence = null;
    if (native) {
      const sourceBytes = readFileSync(resolve(root, native[0]));
      const sourceText = sourceBytes.toString('utf8').replaceAll('\r\n', '\n');
      if (!sourceText.includes(native[2])) throw new Error(`Native source anchor absent: ${id}`);
      nativeEvidence = { source: native[0], sourceSha256: createHash('sha256').update(sourceText, 'utf8').digest('hex'),
        surface: native[1], sourceAnchor: native[2], gap: native[3], verifiedParity: false,
        scope: 'Source candidate only; this mapping does not establish matching contracts, executed tests or CLI/editor acceptance.' };
    }
    operations.push({ method: method.toUpperCase(), path, operationId: id,
      summary: operation.summary ?? '', tags: operation.tags ?? [],
      status: mapping ? 'partial-source-unverified' : 'missing',
      nativeStatus: native ? 'partial-source-unverified' : 'unmapped',
      nativeEvidence,
      agentflowEvidence: mapping ? { source: mapping[0], surface: mapping[1], gap: mapping[2], verifiedParity: false } : null,
      upstreamContract: { parameters: [...(methods.parameters ?? []), ...(operation.parameters ?? [])],
        requestBody: operation.requestBody ?? null, responses: operation.responses ?? {} },
      acceptanceCriteria: acceptance(id, path) });
  }
}
operations.sort((a, b) => a.path.localeCompare(b.path) || a.method.localeCompare(b.method));
for (const id of Object.keys(nativePartial)) if (!operations.some(operation => operation.operationId === id)) throw new Error(`Native mapping is outside the pinned schema: ${id}`);
const inventory = { source: 'https://github.com/anomalyco/opencode/tree/v2.0.16', revision,
  statusScope: 'Historical Python prototype mappings were removed with that implementation. nativeStatus and nativeEvidence describe reviewed C++ source candidates. Unmapped is not proof of absence; no operation is verified parity.',
  nativeSourceHashFormat: 'SHA-256 of UTF-8 source text with CRLF normalized to LF; source candidates may include uncommitted changes at audit time.',
  schema: 'packages/protocol/openapi.json', schemaSha256: createHash('sha256').update(schemaBytes).digest('hex'),
  schemaReferences: 'Resolve $ref values against the pinned source schema. This inventory does not dereference them or cover every non-API behavior.',
  operationCount: operations.length, operations };
const target = resolve(root, 'doc/OPENCODE_API_INVENTORY.json');
writeFileSync(target, JSON.stringify(inventory, null, 2) + '\n');
console.log(`Audited ${operations.length} pinned OpenCode operations: ${operations.filter(x => x.agentflowEvidence).length} historical prototype and ${operations.filter(x => x.nativeEvidence).length} native source candidates; no verified parity claims`);
