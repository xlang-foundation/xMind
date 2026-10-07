'use strict';
const api=acquireVsCodeApi(),byId=id=>document.getElementById(id);
let execution=false,activeRun=false,sessionBusy=false,live,streamText='',streamUsage=null;
const operationSections=new Map();
const processStreams=new Map();
const node=(tag,text,cls)=>{const el=document.createElement(tag);if(text!==undefined)el.textContent=text;if(cls)el.className=cls;return el;};
function markdown(el,text){
  el.innerHTML=DOMPurify.sanitize(marked.parse(text||'',{gfm:true,breaks:false}),{FORBID_TAGS:['style','iframe','form','input','button'],FORBID_ATTR:['style'],ALLOW_DATA_ATTR:false});
  for(const anchor of el.querySelectorAll('a')){anchor.addEventListener('click',event=>{event.preventDefault();api.postMessage({type:'openLink',url:anchor.getAttribute('href')});});}
  for(const block of el.querySelectorAll('pre')){const button=node('button','Copy code','copy-code');button.onclick=()=>api.postMessage({type:'copy',text:block.querySelector('code')?.textContent||block.textContent});block.after(button);}
}
const count=value=>Number.isSafeInteger(value)&&value>=0?value.toLocaleString():'—';
function metrics(el,data={}){const usage=data.usage||{};el.replaceChildren();for(const text of ['Input '+count(usage.prompt_tokens),'Output '+count(usage.completion_tokens),'Total '+count(usage.total_tokens)])el.append(node('span',text));if(Number.isSafeInteger(usage.prompt_tokens_details?.cached_tokens))el.append(node('span','Cached '+count(usage.prompt_tokens_details.cached_tokens)));if(Number.isSafeInteger(usage.completion_tokens_details?.reasoning_tokens))el.append(node('span','Reasoning '+count(usage.completion_tokens_details.reasoning_tokens)));if(data.model)el.append(node('span',data.model));if(Number.isFinite(data.first_token_ms)&&data.first_token_ms>=0)el.append(node('span','First token '+(data.first_token_ms/1000).toFixed(2)+'s'));if(Number.isFinite(data.elapsed_ms)&&data.elapsed_ms>=0)el.append(node('span',(data.elapsed_ms/1000).toFixed(2)+'s'));el.title='Provider-reported tokens and backend-measured timings. A dash means unavailable; token counts are never estimated.';}
function processProposal(section,item){
  let plan;try{plan=JSON.parse(item.arguments_json);}catch{}
  if(!plan||typeof plan.profile_id!=='string'||!Number.isSafeInteger(plan.profile_revision)||plan.profile_revision<1||typeof plan.executable!=='string'||typeof plan.executable_id!=='string'||!plan.executable_id||plan.executable_id.length>512||plan.executable_id.includes('\0')||!Array.isArray(plan.arguments)||plan.arguments.length>64||plan.arguments.some(arg=>typeof arg!=='string')||typeof plan.workdir!=='string'||typeof plan.directory_id!=='string'||!plan.directory_id||!Number.isSafeInteger(plan.timeout_ms)||plan.timeout_ms<1||plan.timeout_ms>600000||!Number.isSafeInteger(plan.output_limit)||plan.output_limit<1||plan.output_limit>4194304){
    section.append(node('p','Command details are unavailable. Refresh from the backend before allowing this operation.','inspection-note'));return false;
  }
  const view=node('div',undefined,'process-proposal');
  view.append(node('strong','Profile '+plan.profile_id+' · revision '+plan.profile_revision),node('p','Directory: '+plan.workdir),node('div','Executable and literal argument vector'),node('pre',JSON.stringify([plan.executable,...plan.arguments],null,2),'process-argv'));
  const binding=node('details');binding.append(node('summary','Executable binding'),node('pre',plan.executable_id));view.append(binding);
  const badges=node('div',undefined,'metrics');badges.append(node('span','Timeout '+(plan.timeout_ms/1000).toLocaleString()+'s'),node('span','Output limit '+count(plan.output_limit)+' bytes'));view.append(badges);
  section.append(view);return true;
}
function processOutcome(value){
  if(!value||typeof value!=='object'||typeof value.operation_id!=='string'||typeof value.profile_id!=='string'||!['exited','cancelled','timed_out'].includes(value.termination)||!Number.isSafeInteger(value.exit_code)||value.exit_code<0||value.exit_code>0xffffffff||!Number.isSafeInteger(value.pid)||value.pid<1||!Number.isFinite(value.elapsed_ms)||value.elapsed_ms<0||typeof value.truncated!=='boolean'||value.process_tree_retired!==true||value.independently_verified!==false)return;
  for(const key of ['stdout','stderr']){
    const channel=value[key];
    if(!channel||!['utf-8','hex'].includes(channel.encoding)||typeof channel.data!=='string'||channel.data.length>524288||(channel.encoding==='hex'&&(!/^(?:[0-9a-fA-F]{2})*$/.test(channel.data)))||!Number.isSafeInteger(channel.byte_count)||!Number.isSafeInteger(channel.retained_bytes)||channel.retained_bytes<0||channel.byte_count<channel.retained_bytes)return;
    if(channel.retained_bytes!==(channel.encoding==='hex'?channel.data.length/2:new TextEncoder().encode(channel.data).length))return;
  }
  return value;
}
function renderProcessOutcome(section,result){
  const view=node('div',undefined,'process-result'),labels={exited:'Exited',cancelled:'Cancelled',timed_out:'Timed out'};
  view.append(node('strong',labels[result.termination]+' · exit '+result.exit_code,result.termination==='exited'&&result.exit_code===0?'':'process-interrupted'));
  const badges=node('div',undefined,'metrics');badges.append(node('span','Profile '+result.profile_id),node('span','PID '+result.pid),node('span',(result.elapsed_ms/1000).toFixed(2)+'s'));view.append(badges);
  for(const key of ['stdout','stderr']){
    const channel=result[key],detail=node('details',undefined,'process-output');
    detail.open=channel.data.length>0;
    detail.append(node('summary',key+' · '+count(channel.byte_count)+' bytes · '+count(channel.retained_bytes)+' retained'+(channel.encoding==='hex'?' · hex':'')));
    // Controls are visible escapes; markup and terminal escapes never execute.
    const visible=channel.encoding==='hex'?channel.data:channel.data.replace(/[\u0000-\u0008\u000b-\u001f\u007f]/g,c=>'\\u'+c.charCodeAt(0).toString(16).padStart(4,'0'));
    detail.append(node('pre',visible,'process-'+key));view.append(detail);
    if(channel.encoding==='utf-8'&&visible!==channel.data)detail.append(node('p','Control bytes are shown as Unicode escapes.','inspection-note'));
  }
  if(result.truncated)view.append(node('p','Output exceeded the retained capture limit. Byte counts include drained output.','inspection-note'));
  view.append(node('p','Exit status and output are observed process results. File or remote effects have not been independently verified.','inspection-note'));
  section.append(view);
}
function resetProcessStreams(){processStreams.clear();byId('process-streams').replaceChildren();}
function processOutput(value){
  if(!value||typeof value.operation_id!=='string'||!value.operation_id||value.operation_id.length>256||typeof value.profile_id!=='string'||!value.profile_id||value.profile_id.length>64||!['stdout','stderr'].includes(value.channel)||value.encoding!=='hex'||!Number.isSafeInteger(value.offset)||value.offset<0||!Number.isSafeInteger(value.retained_bytes)||value.retained_bytes<1||value.retained_bytes>4096||typeof value.data!=='string'||value.data.length!==value.retained_bytes*2||!/^[0-9a-f]+$/.test(value.data)||value.offset+value.retained_bytes>65536)return;
  let state=processStreams.get(value.operation_id);
  if(!state){
    if(processStreams.size>=64){if(!byId('process-stream-limit')){const note=node('p','Additional command output remains in the selected run activity and recorded command results.','inspection-note');note.id='process-stream-limit';byId('process-streams').append(note);}return;}
    const card=node('section',undefined,'process-result process-stream');card.append(node('strong','Captured command output · '+value.profile_id),node('p','Operation '+value.operation_id+' · retained bytes; inspect the recorded operation for its outcome.','inspection-note'));
    state={profile:value.profile_id,card,stdout:{hex:''},stderr:{hex:''}};
    for(const key of ['stdout','stderr']){const detail=node('details',undefined,'process-output');detail.open=true;const summary=node('summary',key+' · 0 retained bytes'),pre=node('pre','');detail.append(summary,pre);card.append(detail);Object.assign(state[key],{summary,pre});}
    byId('process-streams').append(card);processStreams.set(value.operation_id,state);byId('empty').hidden=true;
  }
  const channel=state[value.channel];
  if(state.profile!==value.profile_id||channel.incomplete)return;
  if(value.offset<channel.hex.length/2&&channel.hex.slice(value.offset*2,(value.offset+value.retained_bytes)*2)===value.data)return;
  if(value.offset!==channel.hex.length/2||state.stdout.hex.length/2+state.stderr.hex.length/2+value.retained_bytes>65536){channel.incomplete=true;state.card.append(node('p','Output sequence incomplete. Refresh the run to replay stored bytes.','inspection-note'));return;}
  channel.hex+=value.data;
  const bytes=Uint8Array.from(channel.hex.match(/../g),part=>parseInt(part,16));let text=channel.hex,encoding='hex';
  try{text=new TextDecoder('utf-8',{fatal:true,ignoreBOM:true}).decode(bytes).replace(/[\u0000-\u0008\u000b-\u001f\u007f]/g,c=>'\\u'+c.charCodeAt(0).toString(16).padStart(4,'0'));encoding='UTF-8; controls escaped';}catch{}
  channel.pre.textContent=text;channel.summary.textContent=value.channel+' · '+count(bytes.length)+' retained bytes · '+encoding;
}
function entry(role,data){
  byId('empty').hidden=true;
  const card=node('article',undefined,'message '+role);card.append(node('h4',role==='assistant'?'xMind':role==='user'?'You':role));
  if(role==='tool'){
    let process;try{process=processOutcome(JSON.parse(data.content));}catch{}
    const detail=node('details',undefined,'tool');detail.append(node('summary',process?'Command result · '+process.profile_id:'Tool result'));
    if(process){renderProcessOutcome(detail,process);const raw=node('details');raw.append(node('summary','Raw result'),node('pre',data.content));detail.append(raw);}else detail.append(node('pre',data.content||JSON.stringify(data,null,2)));card.append(detail);
  }
  else {const content=node('div',undefined,'message-body markdown');markdown(content,data.content||data.refusal||'');card.append(content);if(data.tool_calls?.length){const detail=node('details',undefined,'tool');detail.append(node('summary',data.tool_calls.length+' tool request(s)'),node('pre',JSON.stringify(data.tool_calls,null,2)));card.append(detail);}if(role==='assistant'){const info=node('div',undefined,'metrics');metrics(info,data);card.append(info);}}
  byId('history').append(card);return card;
}
function resetLive(){byId('live').replaceChildren();live=undefined;streamText='';streamUsage=null;}
function stream(text){if(!live){live=node('article',undefined,'message assistant streaming');live.append(node('h4','xMind · responding'),node('div',undefined,'message-body markdown'),node('div',undefined,'metrics'));byId('live').append(live);byId('empty').hidden=true;}streamText+=text;markdown(live.querySelector('.message-body'),streamText);metrics(live.querySelector('.metrics'),{usage:streamUsage});}
function operations(items){
  byId('operations').replaceChildren();
  for(const item of items){
    const section=node('section',undefined,'operation');section.append(node('h4',(item.tool==='run_process'?'Command':item.tool)+' · '+item.state));
    const meta=node('details');meta.append(node('summary','Operation '+item.id),node('pre','Workspace: '+item.workspace_id+'\nExpires: '+new Date(item.expires_unix_ms).toISOString()+'\nController: '+(item.decision_actor||'Awaiting decision')),node('pre',item.arguments_json));section.append(meta);
    let reviewable=true;
    if(item.tool==='run_process')reviewable=processProposal(section,item);
    if(['replace_file','create_file'].includes(item.tool)){try{const plan=JSON.parse(item.arguments_json);section.append(node('strong',plan.path));for(const [label,key] of [['Before','before_content'],['After','after_content']])section.append(node('div',label),node('pre',plan[key],label.toLowerCase()));}catch{}}
    if(item.state==='awaiting_approval'){
      for(const decision of ['allow','deny']){
        const labels={replace_file:'Allow edit',create_file:'Allow creation',run_process:'Allow command'};
        const button=node('button',decision==='allow'?(labels[item.tool]||'Allow tool'):'Deny',decision==='allow'?'primary':'');
        button.disabled=Date.now()>=item.expires_unix_ms||(decision==='allow'&&!reviewable);
        button.onclick=()=>{for(const control of section.querySelectorAll('button'))control.disabled=true;api.postMessage({type:'decide',id:item.id,decision});};section.append(button);
      }
    }else{
      if(item.tool==='run_process'){let result;try{result=processOutcome(JSON.parse(item.result_json));}catch{}if(result)renderProcessOutcome(section,result);}
      const detail=node('details');detail.append(node('summary','Outcome'),node('pre',item.result_json));section.append(detail);
    }
    if(item.tool==='run_process'&&item.state==='uncertain')section.append(node('p','Possible command effects remain uncertain. Further effects in this workspace and through this profile are blocked; xMind will not retry the command.','inspection-note'));
    byId('operations').append(section);
  }
}
function send(){if(execution&&!activeRun&&!sessionBusy&&byId('prompt').value.trim())api.postMessage({type:'send',prompt:byId('prompt').value});}
for(const type of ['new','refresh','cancel'])byId(type).onclick=()=>api.postMessage({type});
byId('sessions').onchange=()=>api.postMessage({type:'select',id:byId('sessions').value});byId('send').onclick=send;
byId('runs').onchange=()=>api.postMessage({type:'select-run',id:byId('runs').value});
byId('model').onchange=()=>api.postMessage({type:'model',id:byId('model').value});
byId('prompt').onkeydown=event=>{if(event.key==='Enter'&&!event.shiftKey){event.preventDefault();send();}};
byId('prompt').oninput=()=>{byId('prompt').style.height='auto';byId('prompt').style.height=Math.min(180,byId('prompt').scrollHeight)+'px';};
window.addEventListener('message',({data:m})=>{
  const scroll=byId('scroll'),follow=scroll.scrollHeight-scroll.scrollTop-scroll.clientHeight<80;
  if(m.type==='sessions'){byId('sessions').replaceChildren();if(!m.sessions.length)byId('sessions').append(node('option','No sessions yet'));for(const session of m.sessions){const option=node('option',session.title);option.value=session.id;option.selected=session.id===m.selected;byId('sessions').append(option);}}
  else if(m.type==='runs'){
    sessionBusy=m.busy;byId('run-picker').hidden=!m.runs.length;byId('runs').replaceChildren();
    for(const run of m.runs){const option=node('option',run.state+' · '+run.id);option.value=run.id;option.selected=run.id===m.selected;byId('runs').append(option);}
    byId('send').disabled=!execution||activeRun||sessionBusy;
  }
  else if(m.type==='reset-run'){resetLive();resetProcessStreams();byId('events').textContent='';}
  else if(m.type==='history'||m.type==='transcript'){byId('history').replaceChildren();if(!m.preserveLive)resetLive();if(m.type==='history'){resetProcessStreams();byId('events').textContent='';}byId('empty').hidden=m.history.length>0||!!live||processStreams.size>0;for(const item of m.history)entry(item.role,item.data);}
  else if(m.type==='capabilities'){execution=m.execution;byId('send').disabled=!execution||activeRun||sessionBusy;byId('model').replaceChildren();for(const model of m.models||[]){const option=node('option',model.id);option.value=model.id;option.selected=model.id===m.model;byId('model').append(option);}if(!byId('model').options.length)byId('model').append(node('option',execution?'Backend model · identity unavailable':'No model configured'));byId('model').disabled=!execution||!(m.models||[]).length;if(!execution)byId('status').textContent='Backend connected · configure a model to run an agent';}
  else if(m.type==='user'){entry('user',{content:m.text});resetLive();resetProcessStreams();byId('prompt').value='';byId('events').textContent='';}
  else if(m.type==='draft')byId('prompt').value=m.text;
  else if(m.type==='event'){const event=m.event;byId('events').textContent+=JSON.stringify(event)+'\n';if(event.kind==='process.output')processOutput(event.data);else if(event.kind==='model.text'||event.kind==='model.refusal')stream(event.data.text);else if(event.kind==='model.usage'){streamUsage=event.data;if(!live)stream('');metrics(live.querySelector('.metrics'),{usage:streamUsage});}else if(event.kind==='model.done'){if(live)live.classList.remove('streaming');}else if(event.kind==='conversation.assistant'||event.kind==='conversation.tool_turn')resetLive();}
  else if(m.type==='operations'){
    operationSections.clear();
    operations(m.operations);
    for(const [index,item] of m.operations.entries())if(item.tool==='mcp_tool'){
      const section=byId('operations').children[index];
      try{const plan=JSON.parse(item.arguments_json);section.append(node('p','External tool: '+plan.server_config_id+' / '+plan.peer_tool+' · configuration '+plan.config_revision));}catch{}
      if(item.state==='awaiting_approval')section.append(node('p','The external server can perform effects beyond this workspace. Its read-only hints do not grant permission.'));
      if(item.state==='uncertain')section.append(node('p','The external effect is uncertain. Further effects in this workspace and on this configured server remain blocked.','inspection-note'));
      if(item.state==='succeeded')section.append(node('p','The external server acknowledged this result. xMind has not independently verified its effect.'));
    }
    for(const [index,item] of m.operations.entries())if(item.tool==='replace_file'&&item.state==='uncertain'){
      const section=byId('operations').children[index];operationSections.set(item.id,section);
      section.append(node('p','This edit is uncertain. Further edits in this workspace remain blocked.','inspection-note'));
      const button=node('button','Inspect actual file');button.onclick=()=>api.postMessage({type:'inspect-edit',id:item.id});section.append(button);
    }
    for(const [index,item] of m.operations.entries())if(['replace_file','create_file'].includes(item.tool)&&item.state==='awaiting_approval'){
      const button=node('button','Compare changes');button.disabled=Date.now()>=item.expires_unix_ms;
      button.onclick=()=>api.postMessage({type:'review',id:item.id});
      byId('operations').children[index].append(button);
    }
    for(const [index,item] of m.operations.entries())if(item.tool==='create_file'){
      const section=byId('operations').children[index];
      section.append(node('p','New file. The backend must still verify the recorded parent and absence of this name.'));
      if(item.state==='uncertain')section.append(node('p','Creation is uncertain. Further effects in this workspace remain blocked; the operation will not replay.','inspection-note'));
    }
  }
  else if(m.type==='edit-inspection'){
    const section=operationSections.get(m.id);if(section){
      section.querySelector('.edit-inspection')?.remove();
      const detail=node('div',undefined,'edit-inspection'),value=m.inspection;
      const labels={before:'Matches recorded before state',after:'Matches recorded after state',different:'Differs from the recorded states'};
      detail.append(node('strong',labels[value.match]||'Inspection unavailable'),node('p','Observation only. The outcome remains uncertain and the workspace stays blocked.','inspection-note'));
      detail.append(node('pre','Observed: '+new Date(value.observed_unix_ms).toISOString()+'\nPath: '+value.observed.path+'\nSame file identity: '+(value.same_file?'yes':'no')+'\nSize: '+value.observed.size+' bytes\nSHA-256: '+value.observed.content_sha256));
      section.append(detail);
    }
  }
  else if(m.type==='status'){byId('status').textContent=m.text;activeRun=['queued','running','paused'].includes(m.text);byId('send').disabled=!execution||activeRun||sessionBusy;byId('cancel').hidden=!activeRun;}
  else if(m.type==='error')byId('status').textContent=m.text;
  if(follow)scroll.scrollTop=scroll.scrollHeight;
});api.postMessage({type:'ready'});
