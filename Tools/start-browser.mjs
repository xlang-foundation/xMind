// Start the profile-aware native local view and its loopback HTML adapter.
// The browser connects to the same managed local profiles as the VS Code host.
import {spawn} from 'node:child_process';
import {mkdir,readFile,writeFile,lstat,realpath,stat} from 'node:fs/promises';
import {createHash,randomBytes,randomUUID} from 'node:crypto';
import {resolve,join,relative,isAbsolute} from 'node:path';
import {fileURLToPath} from 'node:url';
import {createBrowserServer} from '../views/browser/server.mjs';
import {verifyNativeRuntime} from '../extensions/vscode/native-runtime.js';

const root=fileURLToPath(new URL('../',import.meta.url));
const options=new Map();
for(let i=2;i<process.argv.length;i+=2){
  if(!['--backend','--state','--assets','--runtime','--workspace','--profile-root','--ready-root','--port','--provider-config'].includes(process.argv[i])||!process.argv[i+1]||options.has(process.argv[i]))throw new Error('Invalid browser launcher arguments');
  options.set(process.argv[i],process.argv[i+1]);
}
if(options.has('--backend')&&['--runtime','--workspace','--profile-root','--ready-root','--provider-config'].some(name=>options.has(name)))throw new Error('--backend cannot be combined with native profile launch options');
const env=process.env;
const localAppData=env.LOCALAPPDATA||join(env.USERPROFILE||root,'AppData','Local');
const state=resolve(options.get('--state')||join(localAppData,'xMind','BrowserView'));
const assets=resolve(options.get('--assets')||join(root,'.agentflow','browser-assets'));
const viewOrigin=options.get('--backend');
const port=Number(options.get('--port')||60405);
if(!Number.isInteger(port)||port<0||port>65535)throw new Error('Invalid browser view port');
await mkdir(state,{recursive:true});

let backend=viewOrigin,nativePid,tokenFile,localAccessToken;
if(!backend){
  const runtime=resolve(options.get('--runtime')||join(root,'extensions','vscode','native-runtime'));
  const verified=await verifyNativeRuntime(runtime);
  const builtServer=join(root,'build','skill-state-native','Release','xmind.exe');
  try{
    const builtHash=createHash('sha256').update(await readFile(builtServer)).digest('hex');
    if(verified.manifest.files['xmind.exe']!==builtHash)throw new Error('The selected verified runtime does not match the current native build. Package the current xmind.exe into a verified runtime and pass it with --runtime.');
  }catch(error){if(error.code!=='ENOENT')throw error;}
  const workspace=await realpath(resolve(options.get('--workspace')||root));
  if(!(await stat(workspace)).isDirectory())throw new Error('The selected workspace is not a directory');
  let profileRoot=resolve(options.get('--profile-root')||join(env.USERPROFILE||localAppData,'.xMind','p'));
  try{profileRoot=await realpath(profileRoot);}catch(error){if(error.code!=='ENOENT')throw error;}
  const readyRoot=resolve(options.get('--ready-root')||env.TEMP||join(localAppData,'Temp'));
  await mkdir(readyRoot,{recursive:true});
  const readyDirectory=join(readyRoot,'browser-view-'+randomUUID());
  const readyFile=join(readyDirectory,'ready.json');
  const token=randomBytes(32).toString('hex');
  tokenFile=join(state,'auth.token');
  await writeFile(tokenFile,token,{flag:'w',mode:0o600});
  localAccessToken=token;
  const runtimeRoot=verified.runtimeRoot;
  const args=['view','--workspace',workspace,'--profile-root',profileRoot,'--ready-file',readyFile];
  let providerConfig=options.get('--provider-config');
  if(!providerConfig){const candidate=join(root,'.config','providers.yaml');try{if((await stat(candidate)).isFile())providerConfig=candidate;}catch(error){if(error.code!=='ENOENT')throw error;}}
  if(providerConfig)args.push('--config',resolve(providerConfig));
  const childEnv={...env,XMIND_VIEW_TOKEN:token};
  for(const key of Object.keys(childEnv))if(key.startsWith('XMIND_UI_')||key==='XMIND_API_KEY'||key==='XMIND_AUTH_TOKEN')delete childEnv[key];
  const child=spawn(join(runtimeRoot,'xmind.exe'),args,{cwd:workspace,env:childEnv,windowsHide:true,detached:true,stdio:['ignore','ignore','pipe']});
  nativePid=child.pid;
  child.unref();
  let failed,nativeError='';
  child.stderr.on('data',bytes=>{if(nativeError.length<8192)nativeError+=bytes.toString('utf8').slice(0,8192-nativeError.length);});
  child.once('error',error=>{failed=error;});
  child.once('exit',(code,signal)=>{failed=new Error(`Native view exited before readiness (${code??signal??'unknown'}).`);});
  const deadline=Date.now()+60000;
  let metadata;
  while(Date.now()<deadline&&!metadata&&!failed){
    try{
      const info=await lstat(readyFile);
      if(!info.isFile()||info.isSymbolicLink()||info.nlink!==1||info.size>16384)throw new Error('Invalid native view metadata file.');
      metadata=JSON.parse(await readFile(readyFile,'utf8'));
    }catch(error){
      if(error.code!=='ENOENT'&&error.code!=='EACCES'&&error.code!=='EPERM'&&error.code!=='EBUSY'&&!(error instanceof SyntaxError))throw error;
      await new Promise(resolve=>setTimeout(resolve,100));
    }
  }
  if(failed||!metadata){
    let detail=failed?.message||'Native view readiness timed out.';
    if(nativeError.trim())detail+=' '+nativeError.trim().slice(0,1024);
    try{const error=JSON.parse(await readFile(join(readyDirectory,'error.json'),'utf8'));if(error.process_id===nativePid&&typeof error.detail==='string')detail=error.detail;}catch{}
    throw new Error(detail);
  }
  if(metadata.process_id!==nativePid||typeof metadata.origin!=='string'||!/^http:\/\/127\.0\.0\.1:[1-9][0-9]{0,4}$/.test(metadata.origin)||metadata.workspace?.configured!==true||typeof metadata.workspace.root!=='string'||metadata.workspace.root.toLowerCase()!==workspace.toLowerCase())throw new Error('Native view metadata does not match the selected workspace.');
  backend=metadata.origin;
}else{
  // Reuse only a credential belonging to this user's active xMind browser
  // launch and only when its recorded native origin matches the attachment.
  // The value remains in this process; it is never sent to browser JavaScript.
  try{
    const activeFile=join(state,'active.json'),activeInfo=await lstat(activeFile);
    if(activeInfo.isFile()&&!activeInfo.isSymbolicLink()&&activeInfo.size<=16384){
      const active=JSON.parse(await readFile(activeFile,'utf8'));
      if(active.backend===backend){
        const candidate=join(state,'auth.token'),tokenInfo=await lstat(candidate);
        if(tokenInfo.isFile()&&!tokenInfo.isSymbolicLink()&&tokenInfo.nlink===1&&tokenInfo.size===64){
          const token=(await readFile(candidate,'utf8')).trim();
          if(/^[0-9a-f]{64}$/.test(token)){tokenFile=candidate;localAccessToken=token;}
        }
      }
    }
  }catch(error){if(error.code!=='ENOENT'&&!(error instanceof SyntaxError))throw error;}
}

const view=await createBrowserServer({backend,assetRoot:assets,localAccessToken});
const url=await view.listen(port)+'/ui/';
await writeFile(join(state,'active.json'),JSON.stringify({url,backend,nativePid,viewPid:process.pid,assets,credentialFile:tokenFile},null,2),{mode:0o600});
console.log('xMind Browser ready at '+url);
console.log(tokenFile?'Native profile-aware browser connected to '+backend+'.':'Attached to the existing native view '+backend+'.');
