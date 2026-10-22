import fs from 'node:fs/promises';
import assert from 'node:assert/strict';
import {createHash} from 'node:crypto';
import {createRequire} from 'node:module';
const repo='D:/CantorAI2026/xMind',root=repo+'/.agentflow/client-delivery-validation/0.1.61/package',stage=root+'/extension-corrected';
const require=createRequire(repo+'/extensions/vscode/package.json'),{pack}=require('@vscode/vsce/out/package.js'),yauzl=require('yauzl');
const sha=b=>createHash('sha256').update(b).digest('hex');
const archive=root+'/agentflow-0.1.61-corrected.vsix';
const entries=await new Promise((resolve,reject)=>yauzl.open(archive,{lazyEntries:true},(error,zip)=>{if(error)return reject(error);const result={};zip.on('error',reject);zip.on('entry',entry=>{if(entry.fileName.endsWith('/')){zip.readEntry();return;}zip.openReadStream(entry,(e,stream)=>{if(e)return reject(e);const chunks=[];stream.on('data',c=>chunks.push(c));stream.on('error',reject);stream.on('end',()=>{const bytes=Buffer.concat(chunks);result[entry.fileName]=sha(bytes);zip.readEntry();});});});zip.on('end',()=>resolve(result));zip.readEntry();}));
const expected=JSON.parse(await fs.readFile(root+'/vsix-receipt.json','utf8')).sourceFiles;
for(const [name,hash] of Object.entries(expected)){if(name==='.vscodeignore')continue;const target=name==='LICENSE'?'LICENSE.txt':name==='README.md'?'readme.md':name;assert.equal(entries['extension/'+target],hash,name);}
async function check(dir,prefix){for(const item of await fs.readdir(dir,{withFileTypes:true})){const name=prefix+'/'+item.name;if(item.isDirectory())await check(dir+'/'+item.name,name);else assert.equal(entries['extension/'+name],sha(await fs.readFile(dir+'/'+item.name)),name);}}
await check(stage+'/native-runtime','native-runtime');
await fs.writeFile(root+'/vsix-corrected-qualified-receipt.json',JSON.stringify({archive,archiveSha256:sha(await fs.readFile(archive)),archiveBytes:(await fs.stat(archive)).size,archiveEntries:Object.keys(entries).length,entries,allStagedRuntimeAndClientFilesVerified:true,installed:false,nativeExecuted:false,releaseAccepted:false},null,2)+'\n',{flag:'wx'});
console.log(JSON.stringify({archive,entries:Object.keys(entries).length,allStagedRuntimeAndClientFilesVerified:true,installed:false,releaseAccepted:false}));
