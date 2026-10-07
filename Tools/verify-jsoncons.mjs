import fs from 'node:fs';
import path from 'node:path';
import crypto from 'node:crypto';
import {fileURLToPath} from 'node:url';
const root=fileURLToPath(new URL('../Native/third_party/jsoncons/',import.meta.url));
const manifest=JSON.parse(fs.readFileSync(path.join(root,'manifest.json'),'utf8'));
if(manifest.revision!=='bcb44594c50c495ee1e690602cdd71455942ad0e' || manifest.version!=='v1.9.0')throw new Error('Unexpected native schema dependency pin');
const actual=new Set();
function walk(dir){for(const entry of fs.readdirSync(dir,{withFileTypes:true})){const file=path.join(dir,entry.name);if(entry.isSymbolicLink())throw new Error('Dependency symlink is not permitted');if(entry.isDirectory())walk(file);else if(entry.isFile())actual.add(path.relative(root,file).split(path.sep).join('/'));}}
walk(path.join(root,'include'));actual.add('LICENSE');
if(actual.size!==Object.keys(manifest.files).length)throw new Error('Native schema dependency file set changed');
for(const [relative,expected] of Object.entries(manifest.files)){
  if(!actual.has(relative) || !/^[a-f0-9]{64}$/.test(expected))throw new Error('Invalid native schema manifest member');
  const file=path.join(root,...relative.split('/'));if(fs.lstatSync(file).isSymbolicLink())throw new Error('Dependency symlink is not permitted');
  const hash=crypto.createHash('sha256').update(fs.readFileSync(file,'utf8').replaceAll('\r\n','\n')).digest('hex');
  if(hash!==expected)throw new Error('Native schema dependency hash changed: '+relative);
}
console.log('Pinned jsoncons native source and license hashes verified: '+actual.size+' files');
