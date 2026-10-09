import fs from 'node:fs';
import path from 'node:path';
import crypto from 'node:crypto';
import {fileURLToPath} from 'node:url';

const pins=[
  {name:'re2',tag:'2025-11-05',revision:'927f5d53caf8111721e734cf24724686bb745f55',count:131,manifest:'6eb28326841b2d5de6e49bdeefc2fc1d88f5ae58d410e4a62d8668e846d5ba57'},
  {name:'abseil-cpp',tag:'20260817.0',revision:'2065f4ded0558c6f89fee67c8e5228feb4eb960e',count:1602,manifest:'d3deac0d03625a870021a1302d8c6e2cd7eec773ff99d5da77544cfe33d40aaf'},
];
const sha=bytes=>crypto.createHash('sha256').update(bytes).digest('hex');
for(const pin of pins){
  const root=fileURLToPath(new URL('../Native/third_party/'+pin.name+'/',import.meta.url));
  const raw=fs.readFileSync(path.join(root,'XMIND_VENDOR.json'));
  if(sha(raw)!==pin.manifest)throw new Error('Regex dependency manifest changed: '+pin.name);
  const manifest=JSON.parse(raw);
  if(manifest.name!==pin.name||manifest.tag!==pin.tag||manifest.revision!==pin.revision||manifest.normalization!=='none; exact upstream Git blob bytes')throw new Error('Unexpected regex dependency pin');
  const actual=new Set();
  function walk(dir){for(const entry of fs.readdirSync(dir,{withFileTypes:true})){
    const file=path.join(dir,entry.name);
    if(entry.isDirectory())walk(file);
    else if(entry.isFile())actual.add(path.relative(root,file).split(path.sep).join('/'));
    else throw new Error('Unsupported regex dependency member: '+entry.name);
  }}
  walk(root);actual.delete('XMIND_VENDOR.json');
  if(actual.size!==pin.count||actual.size!==Object.keys(manifest.files).length)throw new Error('Regex dependency file set changed: '+pin.name);
  for(const [relative,expected]of Object.entries(manifest.files)){
    if(!actual.has(relative)||!/^([A-Za-z0-9_.+-]+\/)*[A-Za-z0-9_.+-]+$/.test(relative)||relative.split('/').some(part=>part==='.'||part==='..')||!/^[a-f0-9]{64}$/.test(expected))throw new Error('Invalid regex dependency member');
    if(sha(fs.readFileSync(path.join(root,...relative.split('/'))))!==expected)throw new Error('Regex dependency bytes changed: '+pin.name+'/'+relative);
  }
  console.log('Pinned '+pin.name+' native source and license verified: '+actual.size+' exact upstream files');
}
