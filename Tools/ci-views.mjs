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
const launcherSources=['Tools/start-browser.mjs','Tools/browser-host-lifecycle.mjs'];
const vendorSources=['marked/lib/marked.umd.js','dompurify/dist/purify.min.js','@xterm/xterm/lib/xterm.js','@xterm/xterm/css/xterm.css','@xterm/addon-fit/lib/addon-fit.js'].map(p=>'extensions/vscode/node_modules/'+p);
const files=[...tracked,...launcherSources,...vendorSources];
if(tracked.length!==64||launcherSources.length!==2||files.length!==71||new Set(files).size!==files.length)throw new Error('View source inventory differs; review and update the complete manifest.');
const sha=b=>createHash('sha256').update(b).digest('hex');
const freeze=async()=>Object.fromEntries(await Promise.all(files.map(async p=>[p,sha(await readFile(join(root,p)))])));
const before=await freeze();
await writeFile(join(out,'source-before.json'),JSON.stringify(before,null,2)+'\n');
const assetRoot=join(root,'build/view-assets'),assetNames=['index.html','patch-review.js','chat.js','chat.css','client.js','marked.js','purify.js','xterm.js','xterm.css','xterm-fit.js','terminal.js','browser.js','browser.css','licenses/marked.txt','licenses/dompurify.txt','licenses/dompurify-MPL.txt','licenses/xterm.txt','licenses/xterm-addon-fit.txt'];
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
// Keep exact counts: a successful but incomplete suite must not pass the gate.
// Review these alongside additions/removals in the complete test inventories.
// Seven extension regressions cover approval ordering, budget notices and
// external turns; five browser regressions cover approval ordering/turns.
// One further extension contract covers UTF-8 reconciliation-note admission.
// Queue-following contracts include terminal drain, explicit inspection and
// successive claims; browser coverage also checks selection-save failures.
// Two placement contracts cover backend snapshots and selector presentation.
// Captured Local admission adds three extension cases and one browser route case;
// the existing browser footer case also belongs in the complete inventory.
// Captured session history adds three extension contracts and two browser
// contracts for scoped paging, receipt validation and proxy admission.
const expectedTests={extension:470,browser:165};
const passed=unchanged&&Object.entries({extension,browser}).every(([name,r])=>r.exitCode===0&&r.tests===expectedTests[name]&&r.passed===expectedTests[name]&&r.failed===0&&r.skipped===0&&r.cancelled===0);
const report={sourceRevision:execFileSync('git',['rev-parse','HEAD'],{cwd:root,encoding:'utf8'}).trim(),scope:'Complete isolated Node adapter/controller suites; fixtures are synthetic, not native inference, live browser or rendered IDE acceptance',sourceFiles:files.length,trackedSourceFiles:tracked.length+launcherSources.length,vendorFiles:vendorSources.length,browserAssets:assetNames.length,sourceAndAssetBytesUnchanged:unchanged,expectedTests,extension,browser,providerRequests:0,nativeExecuted:false,cpythonExecuted:false,passed};
await writeFile(join(out,'gate.json'),JSON.stringify(report,null,2)+'\n');console.log(JSON.stringify(report));
if(!passed)process.exitCode=1;
