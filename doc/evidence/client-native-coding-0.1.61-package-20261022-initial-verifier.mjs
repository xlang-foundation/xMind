import fs from 'node:fs/promises';
import path from 'node:path';
import assert from 'node:assert/strict';
import {createHash} from 'node:crypto';
import {execFileSync} from 'node:child_process';
import {createRequire} from 'node:module';
const repo='D:/CantorAI2026/xMind',root=repo+'/.agentflow/client-delivery-validation/0.1.61',stage=root+'/package/extension';
const require=createRequire(repo+'/extensions/vscode/package.json');
const {pack}=require('@vscode/vsce/out/package.js');
const sha=b=>createHash('sha256').update(b).digest('hex');
const gate=JSON.parse(await fs.readFile(root+'/gate.json','utf8'));assert.equal(gate.passed,true);
await fs.mkdir(stage,{recursive:true});assert.deepEqual(await fs.readdir(stage),[]);
const files=execFileSync('git',['ls-files','-z','extensions/vscode'],{cwd:repo,encoding:'utf8',windowsHide:true}).split('\0').filter(Boolean);
const sourceFiles={};
async function copy(source,relative,expected){const bytes=await fs.readFile(source);if(expected)assert.equal(sha(bytes),expected);await fs.mkdir(path.dirname(stage+'/'+relative),{recursive:true});await fs.writeFile(stage+'/'+relative,bytes,{flag:'wx'});sourceFiles[relative]=sha(bytes);}
for(const file of files){const relative=file.slice('extensions/vscode/'.length);if(relative.startsWith('tests/')||relative.startsWith('scripts/')||relative==='package-lock.json'||relative.endsWith('.vsix'))continue;await copy(repo+'/'+file,relative);}
for(const relative of ['marked/lib/marked.umd.js','marked/LICENSE','dompurify/dist/purify.min.js','dompurify/LICENSE','dompurify/LICENSE-MPL'])await copy(repo+'/extensions/vscode/node_modules/'+relative,'node_modules/'+relative);
const assets=JSON.parse(await fs.readFile(root+'/browser-manifest.json','utf8'));
for(const [file,hash] of Object.entries(assets))await copy(root+'/browser-assets/'+file,'browser-runtime/assets/'+file,hash);
await copy(root+'/server.mjs','browser-runtime/server.mjs');
async function walk(dir,prefix=''){const result={};for(const item of await fs.readdir(dir,{withFileTypes:true})){const name=prefix+item.name;if(item.isDirectory())Object.assign(result,await walk(dir+'/'+item.name,name+'/'));else result[name]=sha(await fs.readFile(dir+'/'+item.name));}return result;}
const runtime=root+'/package/native-runtime',before=await walk(runtime);
await fs.cp(runtime,stage+'/native-runtime',{recursive:true,errorOnExist:true,force:false});
assert.deepEqual(await walk(stage+'/native-runtime'),before);
const archive=root+'/package/agentflow-0.1.61.vsix';assert.equal(await fs.stat(archive).then(()=>true,()=>false),false);
// pack() consumes the staged, already verified assets without executing prepublish.
const packaged=await pack({cwd:stage,packagePath:archive,dependencies:false,allowMissingRepository:false});
assert.equal(packaged.manifest.version,'0.1.61');
assert.deepEqual(await walk(runtime),before);
await fs.writeFile(root+'/package/vsix-receipt.json',JSON.stringify({version:packaged.manifest.version,archive,archiveSha256:sha(await fs.readFile(archive)),archiveBytes:(await fs.stat(archive)).size,files:packaged.files.length,sourceFiles,runtimeFiles:Object.keys(before).length,runtimeUnchanged:true,prepublishExecuted:false,nativeExecuted:false,installed:false,releaseAccepted:false},null,2)+'\n',{flag:'wx'});
console.log(JSON.stringify({archive,files:packaged.files.length,installed:false,releaseAccepted:false}));
