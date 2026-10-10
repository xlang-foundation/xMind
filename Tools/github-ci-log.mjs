// Read-only native CI diagnostics. Git credentials, if required by the logs API,
// stay in process memory and are never printed, written or sent to redirects.
import {spawnSync} from 'node:child_process';
import {mkdir,writeFile} from 'node:fs/promises';
const id=process.argv[2];if(!/^\d+$/.test(id||''))throw new Error('Supply a numeric workflow run ID');
const base='https://api.github.com/repos/xlang-foundation/xMind';let token;
const headers={Accept:'application/vnd.github+json','User-Agent':'xMind-native-verification'};
async function get(url,extra={}){return fetch(url,{headers:{...headers,...extra},redirect:'manual',signal:AbortSignal.timeout(20000)});}
async function authorizedGet(url){let response=await get(url,token?{Authorization:`Bearer ${token}`} : {});if([401,403].includes(response.status)&&!token){const credential=spawnSync('git',['credential','fill'],{input:'protocol=https\nhost=github.com\npath=xlang-foundation/xMind.git\n\n',encoding:'utf8',windowsHide:true,env:{...process.env,GIT_TERMINAL_PROMPT:'0',GCM_INTERACTIVE:'Never'}});if(credential.status!==0)throw new Error('GitHub diagnostics require authentication; configured Git credential lookup failed');token=credential.stdout.split(/\r?\n/).find(line=>line.startsWith('password='))?.slice(9);if(!token)throw new Error('Configured Git credential has no API token');response=await get(url,{Authorization:`Bearer ${token}`});}return response;}
const jobsResponse=await authorizedGet(`${base}/actions/runs/${id}/jobs`);
if(!jobsResponse.ok)throw new Error(`Job metadata HTTP ${jobsResponse.status}`);
const {jobs}=await jobsResponse.json();
for(const job of jobs){
  console.log(JSON.stringify({job:job.name,id:job.id,status:job.status,conclusion:job.conclusion}));
  const annotations=await authorizedGet(job.check_run_url+'/annotations');
  if(annotations.ok)console.log(JSON.stringify({annotations:await annotations.json()}));
  const url=`${base}/actions/jobs/${job.id}/logs`;const logs=await authorizedGet(url);
  let content;
  if(logs.status===302){
    const location=new URL(logs.headers.get('location'));
    if(location.protocol!=='https:'||!['.blob.core.windows.net','.githubusercontent.com','.actions.githubusercontent.com'].some(suffix=>location.hostname.endsWith(suffix)))throw new Error('Unexpected GitHub log redirect destination');
    const download=await fetch(location,{signal:AbortSignal.timeout(20000)});
    if(download.status===404&&job.status==='in_progress'){console.log(JSON.stringify({job:job.name,logs:'not yet published while job is in progress'}));continue;}
    if(!download.ok)throw new Error(`Log download HTTP ${download.status}`);content=await download.text();
  }else if(logs.ok)content=await logs.text();else throw new Error(`Job logs HTTP ${logs.status}`);
  const folder=new URL('../.agentflow/ci/',import.meta.url);await mkdir(folder,{recursive:true});
  await writeFile(new URL(`${id}-${job.id}.log`,folder),content);
  const redact=value=>value.replace(/(?:gh[pousr]_[A-Za-z0-9_]+|github_pat_[A-Za-z0-9_]+)/g,'[redacted GitHub token]');
  console.log(redact(content.split(/\r?\n/).filter(line=>/error|failed|Exception|missing|not found|CTest/i.test(line)).slice(-60).join('\n')));
}
