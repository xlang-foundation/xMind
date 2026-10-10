// Isolated, model-free host/controller validation. No native backend, xlang3,
// interpreter, provider credentials or installed editor is executed/accessed.
import {readFile,writeFile,mkdir} from 'node:fs/promises';
import {createHash} from 'node:crypto';
import {spawn,execFileSync} from 'node:child_process';
import {resolve,join} from 'node:path';
if(process.env.GITHUB_ACTIONS!=='true')throw new Error('Run view contracts on an isolated GitHub runner; use a benchmark-guarded launcher on the development machine.');
const root=resolve(import.meta.dirname,'..'),out=join(root,'build/ci-evidence/views');
await mkdir(out,{recursive:true});
const tracked=execFileSync('git',['ls-files','--','extensions/vscode','views/browser'],{cwd:root,encoding:'utf8'}).trim().split(/\r?\n/);
const files=[...tracked,'extensions/vscode/node_modules/marked/lib/marked.umd.js','extensions/vscode/node_modules/dompurify/dist/purify.min.js'];
if(tracked.length!==42||files.length!==44||new Set(files).size!==files.length)throw new Error('View source inventory differs; review and update the complete manifest.');
const sha=b=>createHash('sha256').update(b).digest('hex');
const freeze=async()=>Object.fromEntries(await Promise.all(files.map(async p=>[p,sha(await readFile(join(root,p)))])));
const before=await freeze();
await writeFile(join(out,'source-before.json'),JSON.stringify(before,null,2)+'\n');
const assetRoot=join(root,'build/view-assets'),assetNames=['index.html','patch-review.js','chat.js','chat.css','client.js','marked.js','purify.js','browser.js','browser.css','licenses/marked.txt','licenses/dompurify.txt','licenses/dompurify-MPL.txt'];
const assets=async()=>Object.fromEntries(await Promise.all(assetNames.map(async p=>[p,sha(await readFile(join(assetRoot,p)))])));
const assetsBefore=await assets();await writeFile(join(out,'assets-before.json'),JSON.stringify(assetsBefore,null,2)+'\n');
const run=(name,args,cwd)=>new Promise((yes,no)=>{
 const child=spawn(process.execPath,args,{cwd,windowsHide:true,env:{...process.env,XMIND_BROWSER_TEST_ASSETS:assetRoot},stdio:['ignore','pipe','pipe']});let log='';
 child.stdout.on('data',b=>log+=b);child.stderr.on('data',b=>log+=b);child.on('error',no);
 child.on('close',async code=>{try{await writeFile(join(out,name+'.log'),log);const number=p=>Number(log.match(new RegExp('# '+p+' (\\d+)'))?.[1]);yes({exitCode:code,tests:number('tests'),passed:number('pass'),failed:number('fail'),skipped:number('skipped'),cancelled:number('cancelled')});}catch(e){no(e);}});
});
const [extension,browser]=await Promise.all([
 run('extension',['--test','tests/*.test.js'],join(root,'extensions/vscode')),
 run('browser',['--test','views/browser/tests/*.test.cjs'],root)
]);
const after=await freeze();await writeFile(join(out,'source-after.json'),JSON.stringify(after,null,2)+'\n');
const assetsAfter=await assets();await writeFile(join(out,'assets-after.json'),JSON.stringify(assetsAfter,null,2)+'\n');
const unchanged=JSON.stringify(before)===JSON.stringify(after)&&JSON.stringify(assetsBefore)===JSON.stringify(assetsAfter);
const passed=unchanged&&extension.exitCode===0&&browser.exitCode===0&&extension.tests===224&&extension.passed===224&&browser.tests===43&&browser.passed===43&&[extension,browser].every(r=>r.failed===0&&r.skipped===0&&r.cancelled===0);
const report={sourceRevision:execFileSync('git',['rev-parse','HEAD'],{cwd:root,encoding:'utf8'}).trim(),scope:'Complete isolated Node adapter/controller suites; fixtures are synthetic, not native inference, live browser or rendered IDE acceptance',sourceFiles:files.length,trackedSourceFiles:tracked.length,vendorFiles:2,browserAssets:assetNames.length,sourceAndAssetBytesUnchanged:unchanged,extension,browser,providerRequests:0,nativeExecuted:false,cpythonExecuted:false,passed};
await writeFile(join(out,'gate.json'),JSON.stringify(report,null,2)+'\n');console.log(JSON.stringify(report));
if(!passed)process.exitCode=1;
