// Download this repository's exact-revision CI evidence/runtime, read-only.
// Authentication stays in memory; redirects never receive Git credentials.
import {spawnSync} from 'node:child_process';
import {mkdir,writeFile} from 'node:fs/promises';
import {fileURLToPath} from 'node:url';
const [id,revision,kind='evidence']=process.argv.slice(2);
if(!/^\d+$/.test(id||'')||!/^[0-9a-f]{40}$/.test(revision||'')||!['evidence','runtime','views'].includes(kind))throw new Error('Supply run ID, exact revision and evidence/runtime/views');
const base='https://api.github.com/repos/xlang-foundation/xMind';let token;
async function get(url){
  if(!url.startsWith(base+'/'))throw new Error('Artifact API URL escaped the repository');
  const request=()=>fetch(url,{headers:{Accept:'application/vnd.github+json','User-Agent':'xMind-native-verification',...(token?{Authorization:`Bearer ${token}`}:{})},redirect:'manual',signal:AbortSignal.timeout(20000)});
  let response=await request();
  if([401,403].includes(response.status)&&!token){
    const credential=spawnSync('git',['credential','fill'],{input:'protocol=https\nhost=github.com\npath=xlang-foundation/xMind.git\n\n',encoding:'utf8',windowsHide:true,env:{...process.env,GIT_TERMINAL_PROMPT:'0',GCM_INTERACTIVE:'Never'}});
    if(credential.status!==0)throw new Error('Configured GitHub authentication unavailable');
    token=credential.stdout.split(/\r?\n/).find(line=>line.startsWith('password='))?.slice(9);
    if(!token)throw new Error('Configured Git credential has no API token');response=await request();
  }
  return response;
}
const runResponse=await get(`${base}/actions/runs/${id}`);if(!runResponse.ok)throw new Error(`Run metadata HTTP ${runResponse.status}`);
const run=await runResponse.json();if(run.head_sha!==revision||run.status!=='completed'||(kind==='runtime'&&run.conclusion!=='success'))throw new Error('Run revision/status does not qualify for the requested artifact');
const artifactsResponse=await get(`${base}/actions/runs/${id}/artifacts`);if(!artifactsResponse.ok)throw new Error(`Artifacts HTTP ${artifactsResponse.status}`);
const data=await artifactsResponse.json();const name=kind==='views'?`view-contracts-evidence-${revision}`:`native-windows-${kind}-${revision}`;
const artifact=data.artifacts.find(item=>item.name===name&&!item.expired);
if(!artifact||artifact.size_in_bytes>268435456)throw new Error('Exact-revision artifact absent, expired or too large');
const redirect=await get(artifact.archive_download_url);if(redirect.status!==302)throw new Error(`Artifact archive HTTP ${redirect.status}`);
const location=new URL(redirect.headers.get('location'));
if(location.protocol!=='https:'||!['.blob.core.windows.net','.githubusercontent.com','.actions.githubusercontent.com'].some(suffix=>location.hostname.endsWith(suffix)))throw new Error('Unexpected artifact redirect');
const download=await fetch(location,{signal:AbortSignal.timeout(60000)});if(!download.ok)throw new Error(`Artifact download HTTP ${download.status}`);
const chunks=[];let bytes=0;for await(const chunk of download.body){bytes+=chunk.length;if(bytes>268435456)throw new Error('Artifact exceeded download limit');chunks.push(chunk);}
const folder=new URL('../.agentflow/ci/artifacts/',import.meta.url);await mkdir(folder,{recursive:true});const target=new URL(name+'.zip',folder);
await writeFile(target,Buffer.concat(chunks));
console.log(JSON.stringify({run:id,revision,conclusion:run.conclusion,artifact:artifact.id,name,bytes,path:fileURLToPath(target)}));
