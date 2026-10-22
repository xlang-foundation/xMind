import fs from 'node:fs/promises';
import assert from 'node:assert/strict';
import {createHash} from 'node:crypto';
import {createRequire} from 'node:module';
const repo='D:/CantorAI2026/xMind',root=repo+'/.agentflow/client-delivery-validation/0.1.61/package',stage=root+'/extension-corrected';
const require=createRequire(repo+'/extensions/vscode/package.json'),{pack}=require('@vscode/vsce/out/package.js'),yauzl=require('yauzl');
const sha=b=>createHash('sha256').update(b).digest('hex');
assert.equal(await fs.stat(stage).then(()=>true,()=>false),false);
await fs.cp(root+'/extension',stage,{recursive:true,errorOnExist:true,force:false});
const dependencyFiles={};
async function copyTree(source,target,prefix){for(const item of await fs.readdir(source,{withFileTypes:true})){assert.equal(item.isSymbolicLink(),false);const from=source+'/'+item.name,to=target+'/'+item.name,name=prefix+'/'+item.name;if(item.isDirectory()){await fs.mkdir(to,{recursive:true});await copyTree(from,to,name);}else {const bytes=await fs.readFile(from);const existing=await fs.readFile(to).catch(e=>{if(e.code!=='ENOENT')throw e;return null;});if(existing)assert.deepEqual(existing,bytes);else await fs.writeFile(to,bytes,{flag:'wx'});dependencyFiles[name]=sha(bytes);}}}
for(const name of ['marked','dompurify','@xterm/xterm','@xterm/addon-fit','@types/trusted-types']){await fs.mkdir(stage+'/node_modules/'+name,{recursive:true});await copyTree(repo+'/extensions/vscode/node_modules/'+name,stage+'/node_modules/'+name,name);}
const archive=root+'/agentflow-0.1.61-corrected.vsix';assert.equal(await fs.stat(archive).then(()=>true,()=>false),false);
const packaged=await pack({cwd:stage,packagePath:archive,dependencies:true,useYarn:false});
const entries=await new Promise((resolve,reject)=>yauzl.open(archive,{lazyEntries:true},(error,zip)=>{if(error)return reject(error);const result={};zip.on('error',reject);zip.on('entry',entry=>{if(entry.fileName.endsWith('/')){zip.readEntry();return;}zip.openReadStream(entry,(e,stream)=>{if(e)return reject(e);const chunks=[];stream.on('data',c=>chunks.push(c));stream.on('error',reject);stream.on('end',()=>{const bytes=Buffer.concat(chunks);result[entry.fileName]=sha(bytes);zip.readEntry();});});});zip.on('end',()=>resolve(result));zip.readEntry();}));
const expected=JSON.parse(await fs.readFile(root+'/vsix-receipt.json','utf8')).sourceFiles;
for(const [name,hash] of Object.entries(expected)){const target=name==='LICENSE'?'LICENSE.txt':name==='README.md'?'readme.md':name;assert.equal(entries['extension/'+target],hash,name);}
const runtimeManifest=JSON.parse(await fs.readFile(stage+'/native-runtime/manifest.json','utf8').catch(e=>{if(e.code==='ENOENT')return '{}';throw e;}));
async function check(dir,prefix){for(const item of await fs.readdir(dir,{withFileTypes:true})){const name=prefix+'/'+item.name;if(item.isDirectory())await check(dir+'/'+item.name,name);else assert.equal(entries['extension/'+name],sha(await fs.readFile(dir+'/'+item.name)),name);}}
await check(stage+'/native-runtime','native-runtime');
await fs.writeFile(root+'/vsix-corrected-receipt.json',JSON.stringify({archive,archiveSha256:sha(await fs.readFile(archive)),archiveBytes:(await fs.stat(archive)).size,archiveEntries:Object.keys(entries).length,packagedFiles:packaged.files.length,entries,dependencyFiles,allStagedRuntimeAndClientFilesVerified:true,installed:false,nativeExecuted:false,releaseAccepted:false},null,2)+'\n',{flag:'wx'});
console.log(JSON.stringify({archive,entries:Object.keys(entries).length,allStagedRuntimeAndClientFilesVerified:true,installed:false,releaseAccepted:false}));
