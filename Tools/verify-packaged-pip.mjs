// Actual packaged xlang3 bootstrap and offline installation. No CPython,
// provider request, shared configuration or existing workspace is used.
import fs from 'node:fs/promises';
import path from 'node:path';
import {spawn} from 'node:child_process';
import {createHash} from 'node:crypto';
import assert from 'node:assert/strict';
import runtime from '../extensions/vscode/native-runtime.js';
const args=process.argv.slice(2);assert.equal(args.length,3,'Supply runtime, bundled wheel directory and evidence directory.');
assert.ok(process.platform==='win32'&&process.arch==='x64','This package smoke requires Windows x64.');
const [root,wheels,evidence]=args.map(value=>path.resolve(value)),sha=value=>createHash('sha256').update(value).digest('hex');
const verified=await runtime.verifyNativeRuntime(root),bootstrap=verified.manifest.pipBootstrap;
assert.ok(bootstrap&&/^[0-9]+(?:\.[0-9]+){1,3}$/.test(bootstrap.version)&&/^[a-f0-9]{64}$/.test(bootstrap.wheelSha256));
const name=`pip-${bootstrap.version}-py3-none-any.whl`;assert.equal(bootstrap.source,'ensurepip/_bundled/'+name);
const wheel=path.join(wheels,name),bytes=await fs.readFile(wheel);assert.equal(sha(bytes),bootstrap.wheelSha256);
const workspace=path.join(evidence,'packaged-pip-workspace'),target=path.join(workspace,'site');
for(const input of [root,wheels]){const relative=path.relative(input,workspace);assert.ok(path.isAbsolute(relative)||relative==='..'||relative.startsWith('..'+path.sep),'Test output must be outside package/source inputs.');}
await fs.mkdir(evidence,{recursive:true});await fs.mkdir(workspace);const env={...process.env};
for(const name of Object.keys(env))if(/^(?:XMIND_|PIP_|OPENAI_|ANTHROPIC_|GEMINI_|DEEPSEEK_)/i.test(name))delete env[name];
Object.assign(env,{XLANG3_PYTHON_LIB:path.join(root,'stdlib'),PYTHONPATH:path.join(root,'modules'),PIP_CONFIG_FILE:process.platform==='win32'?'NUL':'/dev/null',PIP_DISABLE_PIP_VERSION_CHECK:'1',PIP_NO_INPUT:'1'});
const program=path.join(root,'xlang3.exe'),commands=[];
async function command(arguments_){const result=await new Promise((resolve,reject)=>{const child=spawn(program,['-B',...arguments_],{cwd:workspace,env,windowsHide:true,stdio:['ignore','pipe','pipe']});let stdout='',stderr='';child.stdout.on('data',b=>stdout+=b);child.stderr.on('data',b=>stderr+=b);child.once('error',reject);child.once('close',(exitCode,signal)=>resolve({arguments:['-B',...arguments_],exitCode,signal,stdout,stderr}));});commands.push(result);await fs.writeFile(path.join(evidence,'packaged-pip-commands.json'),JSON.stringify(commands,null,2)+'\n');assert.equal(result.exitCode,0,result.stderr);return result;}
const identity=await command(['-c',"import sys; assert sys.implementation.name == 'xlang3'; print(sys.implementation.name)"]);assert.equal(identity.stdout.trim(),'xlang3');
const version=await command(['-m','pip','--version']);assert.ok(version.stdout.startsWith('pip '+bootstrap.version+' '));
const install=await command(['-m','pip','install','--no-index','--only-binary=:all:','--target',target,wheel]);assert.ok(install.stdout.includes('Successfully installed pip-'+bootstrap.version));
assert.ok((await fs.stat(path.join(target,'pip','__main__.py'))).isFile());
let scripts=0;for(const directory of ['bin','Scripts']){let names;try{names=await fs.readdir(path.join(target,directory));}catch(error){if(error.code==='ENOENT')continue;throw error;}for(const name of names.filter(name=>/^pip(?:3(?:\.14)?)?(?:\.exe)?$/.test(name))){const bytes=await fs.readFile(path.join(target,directory,name));assert.ok(bytes.includes(Buffer.from('xlang3.exe')),'Generated pip script must select xlang3');scripts++;}}
assert.ok(scripts>0,'Pip must generate its console entry points.');
const result={manifestSha256:verified.manifestSha256,nativeRevision:verified.manifest.nativeRevision,sdkRevision:verified.manifest.sdkRevision,pip:bootstrap.version,wheelSha256:bootstrap.wheelSha256,commands:commands.length,allExitZero:true,offlineInstall:true,dependenciesSkipped:false,generatedScripts:scripts,generatedScriptsUseXlang3:true,cpythonExecuted:false,scope:'Actual packaged CLI bootstrap and complete offline pure pip installation; no model, backend or rendered client acceptance.'};
await fs.writeFile(path.join(evidence,'packaged-pip.json'),JSON.stringify(result,null,2)+'\n',{flag:'wx'});console.log(JSON.stringify(result));
