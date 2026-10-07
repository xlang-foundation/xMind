// Read-only status for this repository's native workflow. No credentials are
// needed for public run metadata, and no dispatch/cancel/write is performed.
const revision=process.argv[2];
if(!/^[0-9a-f]{40}$/.test(revision||''))throw new Error('Supply the exact committed revision');
const url='https://api.github.com/repos/xlang-foundation/xMind/actions/workflows/native-windows.yml/runs?branch=checkpoint%2Fnative-persistence-m1&per_page=10';
const response=await fetch(url,{headers:{Accept:'application/vnd.github+json','User-Agent':'xMind-native-verification'},signal:AbortSignal.timeout(15000)});
if(!response.ok)throw new Error(`GitHub workflow metadata returned HTTP ${response.status}`);
const data=await response.json();const run=data.workflow_runs.find(item=>item.head_sha===revision);
if(!run){console.log(JSON.stringify({revision,status:'not_observed',scope:'No matching native workflow run returned yet'}));}
else{
  console.log(JSON.stringify({revision,id:run.id,status:run.status,conclusion:run.conclusion,url:run.html_url,created_at:run.created_at,updated_at:run.updated_at}));
  const jobsResponse=await fetch(run.jobs_url,{headers:{Accept:'application/vnd.github+json','User-Agent':'xMind-native-verification'},signal:AbortSignal.timeout(15000)});
  if(jobsResponse.ok){const jobs=await jobsResponse.json();console.log(JSON.stringify({jobs:jobs.jobs.map(job=>({name:job.name,status:job.status,conclusion:job.conclusion,steps:job.steps.map(step=>({name:step.name,status:step.status,conclusion:step.conclusion}))}))}));}
}
