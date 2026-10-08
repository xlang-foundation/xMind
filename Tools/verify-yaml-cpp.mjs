import fs from 'node:fs';
import path from 'node:path';
import crypto from 'node:crypto';
import {fileURLToPath} from 'node:url';
const root=fileURLToPath(new URL('../Native/third_party/yaml-cpp/',import.meta.url));
const manifest=JSON.parse(fs.readFileSync(path.join(root,'XMIND_VENDOR.json'),'utf8'));
if(manifest.revision!=='56e3bb550c91fd7005566f19c079cb7a503223cf'||manifest.tag!=='yaml-cpp-0.9.0'||manifest.normalization!=='none; exact upstream Git blob bytes')throw new Error('Unexpected native YAML dependency pin');
const actual=new Set();
function walk(dir){for(const entry of fs.readdirSync(dir,{withFileTypes:true})){
  const file=path.join(dir,entry.name);
  if(entry.isSymbolicLink())throw new Error('Native YAML dependency symlink is not permitted');
  if(entry.isDirectory())walk(file);
  else if(entry.isFile())actual.add(path.relative(root,file).split(path.sep).join('/'));
}}
walk(root);actual.delete('XMIND_VENDOR.json');
if(actual.size!==101||actual.size!==Object.keys(manifest.files).length)throw new Error('Native YAML dependency file set changed');
for(const [relative,expected]of Object.entries(manifest.files)){
  if(!actual.has(relative)||!/^([A-Za-z0-9_.-]+\/)*[A-Za-z0-9_.-]+$/.test(relative)||relative.split('/').some(part=>part==='.'||part==='..')||!/^[a-f0-9]{64}$/.test(expected))throw new Error('Invalid native YAML manifest member');
  const hash=crypto.createHash('sha256').update(fs.readFileSync(path.join(root,...relative.split('/')))).digest('hex');
  if(hash!==expected)throw new Error('Native YAML dependency bytes changed: '+relative);
}
console.log('Pinned yaml-cpp native source and license verified: '+actual.size+' exact upstream files');
