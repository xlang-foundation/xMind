// Read-only native workflow status. Public metadata is tried first; configured
// Git credentials are used in memory if GitHub requires authentication or rate
// limits anonymous reads. No dispatch/cancel/write or credential output.
import {spawnSync} from 'node:child_process';
const revision=process.argv[2];
if(!/^[0-9a-f]{40}$/.test(revision||''))throw new Error('Supply the exact committed revision');
const base='https://api.github.com/repos/xlang-foundation/xMind';let token;
async function get(url){
  if(!url.startsWith(base+'/actions/'))throw new Error('Workflow metadata URL escaped this repository');
  const request=()=>fetch(url,{headers:{Accept:'application/vnd.github+json','User-Agent':'xMind-native-verification',...(token?{Authorization:'Bearer '+token}:{})},redirect:'error',signal:AbortSignal.timeout(15000)});
  let result=await request();
  if([401,403].includes(result.status)&&!token){
    const credential=spawnSync('git',['credential','fill'],{input:'protocol=https\nhost=github.com\npath=xlang-foundation/xMind.git\n\n',encoding:'utf8',windowsHide:true,env:{...process.env,GIT_TERMINAL_PROMPT:'0',GCM_INTERACTIVE:'Never'}});
    if(credential.status!==0)throw new Error('Configured GitHub authentication unavailable');
    token=credential.stdout.split(/\r?\n/).find(line=>line.startsWith('password='))?.slice(9);
    if(!token)throw new Error('Configured GitHub authentication has no API token');result=await request();
  }
  return result;
}
const url=base+'/actions/workflows/native-windows.yml/runs?branch=checkpoint%2Fnative-persistence-m1&per_page=10';
const response=await get(url);
if(!response.ok)throw new Error(`GitHub workflow metadata returned HTTP ${response.status}`);
const data=await response.json();const run=data.workflow_runs.find(item=>item.head_sha===revision);
if(!run){console.log(JSON.stringify({revision,status:'not_observed',scope:'No matching native workflow run returned yet'}));}
else{
  console.log(JSON.stringify({revision,id:run.id,status:run.status,conclusion:run.conclusion,url:run.html_url,created_at:run.created_at,updated_at:run.updated_at}));
  const jobsResponse=await get(run.jobs_url);
  if(jobsResponse.ok){const jobs=await jobsResponse.json();console.log(JSON.stringify({jobs:jobs.jobs.map(job=>({name:job.name,status:job.status,conclusion:job.conclusion,steps:job.steps.map(step=>({name:step.name,status:step.status,conclusion:step.conclusion}))}))}));}
}
