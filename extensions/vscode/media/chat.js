'use strict';
const api=globalThis.xMindView||acquireVsCodeApi(),byId=id=>document.getElementById(id);
let execution=false,activeRun=false,sessionBusy=false,live,streamText='',streamUsage=null;
let renameCapability=false,renameSnapshot;
const operationSections=new Map();
const processStreams=new Map();
const node=(tag,text,cls)=>{const el=document.createElement(tag);if(text!==undefined)el.textContent=text;if(cls)el.className=cls;return el;};
function markdown(el,text){
  el.innerHTML=DOMPurify.sanitize(marked.parse(text||'',{gfm:true,breaks:false}),{FORBID_TAGS:['style','iframe','form','input','button'],FORBID_ATTR:['style'],ALLOW_DATA_ATTR:false});
  for(const anchor of el.querySelectorAll('a')){anchor.addEventListener('click',event=>{event.preventDefault();api.postMessage({type:'openLink',url:anchor.getAttribute('href')});});}
  for(const block of el.querySelectorAll('pre')){const button=node('button','Copy code','copy-code');button.onclick=()=>api.postMessage({type:'copy',text:block.querySelector('code')?.textContent||block.textContent});block.after(button);}
}
const count=value=>Number.isSafeInteger(value)&&value>=0?value.toLocaleString():'—';
const providerWireLabels={'chat-completions':'Chat Completions',responses:'Responses','anthropic-messages':'Messages','gemini-generate-content':'GenerateContent'};
const providerWireLabel=value=>typeof value==='string'&&Object.hasOwn(providerWireLabels,value)?providerWireLabels[value]:undefined;
const providerLabel=value=>value==='openai'?'OpenAI':value==='anthropic'?'Claude':value==='gemini'?'Gemini':value;
function profileBadge(value){
  const fields=['profile_id','profile_revision','route_id','provider','wire','model_id'];
  if(!value||typeof value!=='object'||Array.isArray(value)||Object.keys(value).length!==fields.length||fields.some(field=>!Object.hasOwn(value,field))||!Number.isSafeInteger(value.profile_revision)||value.profile_revision<1)return;
  for(const field of ['profile_id','route_id','provider','model_id'])if(typeof value[field]!=='string'||!value[field]||value[field].length>256||value[field].startsWith('sk-')||!/^[A-Za-z0-9_.:/-]+$/.test(value[field]))return;
  const wire=providerWireLabel(value.wire);if(!wire)return;
  const badge=node('span',providerLabel(value.provider)+' · '+wire,'provider-context');badge.title='Provider profile: '+value.profile_id+'\nProfile version: '+value.profile_revision+'\nRoute: '+value.route_id+'\nModel: '+value.model_id;return badge;
}
const guidanceLiteral=value=>value.replace(/[\u0000-\u001f\u007f]/g,c=>'\\u'+c.charCodeAt(0).toString(16).padStart(4,'0'));
function renderRunContext(run){
  const area=byId('run-context');area.replaceChildren();area.hidden=true;
  const badge=profileBadge(run?.provider_context);if(!badge)return;
  area.append(node('span','Admitted profile:'),badge,node('span',run.provider_context.model_id));area.hidden=false;
}
function metrics(el,data={}){
  const usage=data.usage||{},uncached=usage.input_tokens_scope==='uncached';el.replaceChildren();
  for(const text of [(uncached?'Input (uncached) ':'Input ')+count(usage.prompt_tokens),'Output '+count(usage.completion_tokens),'Total '+count(usage.total_tokens)])el.append(node('span',text));
  if(Number.isSafeInteger(usage.cache_creation_input_tokens)&&usage.cache_creation_input_tokens>=0)el.append(node('span','Cache write '+count(usage.cache_creation_input_tokens)));
  if(Number.isSafeInteger(usage.prompt_tokens_details?.cached_tokens))el.append(node('span','Cached '+count(usage.prompt_tokens_details.cached_tokens)));
  if(Number.isSafeInteger(usage.completion_tokens_details?.reasoning_tokens))el.append(node('span','Reasoning '+count(usage.completion_tokens_details.reasoning_tokens)));
  if(data.model)el.append(node('span',data.model));const profile=profileBadge(data.provider_context);if(profile)el.append(profile);
  if(Number.isFinite(data.first_token_ms)&&data.first_token_ms>=0)el.append(node('span','First token '+(data.first_token_ms/1000).toFixed(2)+'s'));
  if(Number.isFinite(data.elapsed_ms)&&data.elapsed_ms>=0)el.append(node('span',(data.elapsed_ms/1000).toFixed(2)+'s'));
  el.title='Provider-reported tokens and backend-measured timings. A dash means unavailable; token counts are never estimated.'+(uncached?' Input (uncached) excludes cache writes and reads.':'');
}
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
function entry(role,data,parent=byId('history')){
  byId('empty').hidden=true;
  const card=node('article',undefined,'message '+role);card.append(node('h4',role==='assistant'?'xMind':role==='user'?'You':role));
  if(role==='tool'){
    let process;try{process=processOutcome(JSON.parse(data.content));}catch{}
    const detail=node('details',undefined,'tool');detail.append(node('summary',process?'Command result · '+process.profile_id:'Tool result'));
    if(process){renderProcessOutcome(detail,process);const raw=node('details');raw.append(node('summary','Raw result'),node('pre',data.content));detail.append(raw);}else detail.append(node('pre',data.content||JSON.stringify(data,null,2)));card.append(detail);
  }
  else if(role==='assistant'&&data.source==='graph_join'&&Array.isArray(data.nodes)&&typeof data.graph_id==='string'){
    card.append(node('p','Completed graph '+data.graph_id+'.'),node('p','Observed node outputs are below. Provider metrics remain on the agent responses.','inspection-note'));
    const detail=node('details',undefined,'tool');detail.append(node('summary','Observed join outputs · '+data.nodes.length+' nodes'),node('pre',JSON.stringify(data.nodes,null,2)));card.append(detail);
  }
  else {const content=node('div',undefined,'message-body markdown');markdown(content,data.content||data.refusal||'');card.append(content);if(data.tool_calls?.length){const detail=node('details',undefined,'tool');detail.append(node('summary',data.tool_calls.length+' tool request(s)'),node('pre',JSON.stringify(data.tool_calls,null,2)));card.append(detail);}if(role==='assistant'){const info=node('div',undefined,'metrics');metrics(info,data);card.append(info);}}
  parent.append(card);return card;
}
function resetLive(){byId('live').replaceChildren();live=undefined;streamText='';streamUsage=null;}
function resetFailure(){byId('run-failure').replaceChildren();byId('run-failure').hidden=true;}
const protocolDiagnosticCodes=new Set([
  'responses_function_final_mismatch','responses_arguments_mismatch','responses_terminal_mismatch',
  'responses_event_unsupported','responses_item_unsupported','responses_reasoning_continuation_missing',
  'responses_reasoning_incomplete','responses_item_incomplete','responses_turn_incomplete',
  'responses_item_lifecycle_invalid','responses_function_lifecycle_invalid','responses_function_start_invalid',
  'provider_json_invalid','responses_json_invalid','responses_stream_incomplete','responses_provider_incomplete',
  'responses_sequence_invalid','responses_event_type_mismatch','responses_item_identity_mismatch',
  'responses_identity_mismatch','responses_model_identity_mismatch','responses_final_identity_mismatch',
  'responses_message_mismatch','responses_text_mismatch','responses_summary_mismatch',
  'responses_usage_invalid','responses_limit_exceeded','chat_stream_incomplete'
]);
function runFailure(data){
  const card=byId('run-failure');card.replaceChildren();card.hidden=false;byId('empty').hidden=true;
  card.append(node('h4','Selected run failed'));
  const status=data?.status;
  card.append(node('p',data?.reason==='provider_http_error'&&Number.isInteger(status)&&status>=100&&status<=599
    ?'The model provider returned HTTP '+status+'. This run stopped before completing its response.'
    :data?.reason==='incompatible_provider_history'
    ?'This conversation contains provider history that the selected model’s wire cannot use. Start a new conversation or select a model using the previous wire. The recorded history is preserved.'
    :data?.reason==='model_protocol_error'
    ?'The model response could not be validated, so this run stopped. Review the recorded tool outcomes before starting another run.'
    :'Execution failed. Inspect the selected run activity for the recorded reason.'));
  if(data?.reason==='model_protocol_error'&&protocolDiagnosticCodes.has(data.protocol_error_code)){
    const detail=node('details');detail.append(node('summary','Recorded model protocol diagnostic'),node('pre',data.protocol_error_code));card.append(detail);
  }
  if(data?.reason==='provider_http_error'){
    const fields=[
      ['Type','provider_error_type',['invalid_request_error','authentication_error','permission_error','rate_limit_error','server_error','insufficient_quota']],
      ['Code','provider_error_code',['unsupported_parameter','unsupported_value','invalid_value','missing_required_parameter','model_not_found','invalid_api_key','insufficient_quota','context_length_exceeded','rate_limit_exceeded']],
      ['Parameter','provider_error_param',['model','messages','tools','tool_choice','n','stream','stream_options','stream_options.include_usage','max_completion_tokens','max_tokens','temperature','top_p','reasoning_effort','response_format','input','instructions','max_output_tokens']]
    ],lines=[];
    for(const [label,key,allowed] of fields)if(allowed.includes(data[key]))lines.push(label+': '+data[key]);
    if(lines.length){const detail=node('details');detail.open=true;detail.append(node('summary','Recorded provider diagnostics'),node('pre',lines.join('\n')));card.append(detail);}
  }
  card.append(node('p','This is a backend execution result. Select another run above to inspect its outcome.','inspection-note'));
  if(live)live.classList.remove('streaming');
}
function stream(text){if(!live){live=node('article',undefined,'message assistant streaming');live.append(node('h4','xMind · responding'),node('div',undefined,'message-body markdown'),node('div',undefined,'metrics'));byId('live').append(live);byId('empty').hidden=true;}streamText+=text;markdown(live.querySelector('.message-body'),streamText);metrics(live.querySelector('.metrics'),{usage:streamUsage});}
function operations(items){
  byId('operations').replaceChildren();
  for(const item of items){
    const section=node('section',undefined,'operation');section.append(node('h4',(item.node_id?'Node '+item.node_id+' · ':'')+(item.tool==='run_process'?'Command':item.tool)+' · '+item.state));
    const meta=node('details');meta.append(node('summary','Operation '+item.id),node('pre','Workspace: '+item.workspace_id+'\nExpires: '+new Date(item.expires_unix_ms).toISOString()+'\nController: '+(item.decision_actor||'Awaiting decision')),node('pre',item.arguments_json));section.append(meta);
    let reviewable=true;
    if(item.tool==='run_process')reviewable=processProposal(section,item);
    if(['replace_file','create_file'].includes(item.tool)){try{const plan=JSON.parse(item.arguments_json);section.append(node('strong',plan.path));for(const [label,key] of [['Before','before_content'],['After','after_content']])section.append(node('div',label),node('pre',plan[key],label.toLowerCase()));}catch{}}
    if(['replace_file','create_file','run_process'].includes(item.tool)){
      try{
        const guidance=JSON.parse(item.arguments_json).repository_guidance;
        if(guidance!==undefined){
          if(!guidance || guidance.version!==1 || typeof guidance.directory!=='string' || !guidance.directory || guidance.directory.length>4096 || !Array.isArray(guidance.sources) || guidance.sources.length>33)throw new Error('Malformed guidance binding');
          const lines=['Directory: '+guidanceLiteral(guidance.directory)];
          for(const source of guidance.sources){
            if(!source || typeof source.path!=='string' || !source.path || source.path.length>8192 || typeof source.file_id!=='string' || !source.file_id || typeof source.workspace_id!=='string' || !source.workspace_id || typeof source.content_sha256!=='string' || !/^[a-f0-9]{64}$/.test(source.content_sha256) || !Number.isSafeInteger(source.byte_count) || source.byte_count<0 || source.byte_count>16384)throw new Error('Malformed guidance source');
            lines.push(guidanceLiteral(source.path)+' · '+source.byte_count+' bytes\nSHA-256: '+source.content_sha256);
          }
          if(!guidance.sources.length)lines.push('No AGENTS.md sources in this scope at proposal time.');
          const detail=node('details',undefined,'guidance-binding');detail.append(node('summary','Repository guidance bound to approval'),node('pre',lines.join('\n\n')),node('p','The backend rechecks after approval. A mismatch retires this proposal without dispatch; a new call requires a new approval.'));section.append(detail);
        }
      }catch{reviewable=false;section.append(node('p','Repository guidance binding is malformed. Allow is unavailable; inspect the recorded operation.','inspection-note'));}
    }
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
const graphRows=new Map();let observedGraph,workflowExecutable=false;
const canExecute=()=>byId('workflow').value?workflowExecutable:execution;
function clearGraph(){graphRows.clear();observedGraph=undefined;byId('graph-view').replaceChildren();byId('graph-view').hidden=true;}
function graphView(record,children,histories){
  const root=record.run.id;if(observedGraph!==root){clearGraph();observedGraph=root;const heading=node('h3',record.graph_id+' · revision '+record.graph_revision);byId('graph-view').append(heading);}
  byId('graph-view').hidden=false;const definitions=new Map(record.spec.nodes.map(n=>[n.id,n]));
  for(const state of record.checkpoint.nodes){
    let row=graphRows.get(state.id);
    if(!row){const card=node('section',undefined,'graph-node'),heading=node('h4'),body=node('div',undefined,'graph-output'),responses=node('div',undefined,'graph-responses'),stream=node('div',undefined,'graph-live');card.append(heading,body,responses,stream);row={card,heading,body,responses,stream,text:'',usage:undefined};graphRows.set(state.id,row);byId('graph-view').append(card);}
    const definition=definitions.get(state.id),child=children.find(c=>c.node_id===state.id);row.heading.textContent=state.id+' · '+definition.type+' · '+state.state;
    const waiting=definition.type==='human'&&state.state==='waiting_human'&&['paused','running'].includes(record.run.state);
    if(waiting){
      if(!row.form){row.body.replaceChildren();row.body.append(node('p',definition.prompt||'Provide input'));const form=node('form',undefined,'graph-input'),label=node('label','JSON input'),input=node('textarea'),button=node('button','Submit answer','primary');input.setAttribute('aria-label','JSON input for '+state.id);input.placeholder='{}';input.maxLength=65536;button.type='submit';label.append(input);form.append(label,node('p','Input supplies data; it does not approve tools or commands.','inspection-note'),button);row.body.append(form);row.form=form;row.input=input;row.button=button;
        form.onsubmit=event=>{event.preventDefault();button.disabled=true;api.postMessage({type:'graph-input',root:observedGraph,node:state.id,revision:Number(form.dataset.revision),input_json:input.value});};
      }row.form.dataset.revision=String(record.checkpoint_revision);row.button.disabled=false;
    }else{
      const signature=JSON.stringify(state.output);if(row.form||row.outputSignature!==signature){row.form=undefined;row.body.replaceChildren();if(state.output!==undefined){const detail=node('details');detail.append(node('summary','Observed output'),node('pre',JSON.stringify(state.output,null,2)));row.body.append(detail);}row.outputSignature=signature;}
    }
    if(child && histories[child.id]){const history=histories[child.id],signature=JSON.stringify(history);if(row.historySignature!==signature){row.responses.replaceChildren();for(const item of history)if(item.role!=='user')entry(item.role,item.data,row.responses);row.historySignature=signature;row.stream.replaceChildren();row.text='';row.usage=undefined;}}
  }
}
function graphEvent(message){
  const event=message.event;byId('events').textContent+=JSON.stringify(event)+'\n';if(event.kind==='process.output'){processOutput(event.data);return;}
  const row=graphRows.get(message.node_id);if(!row)return;
  if(event.kind==='conversation.assistant'||event.kind==='conversation.tool_turn'){row.stream.replaceChildren();row.text='';row.usage=undefined;return;}
  if(!['model.text','model.refusal','model.usage','model.done'].includes(event.kind))return;
  if(event.kind==='model.text'||event.kind==='model.refusal')row.text+=event.data.text;
  if(event.kind==='model.usage')row.usage=event.data;
  if(!row.stream.children.length){const article=node('article',undefined,'message assistant streaming');article.append(node('h4','xMind · '+message.node_id),node('div',undefined,'message-body markdown'),node('div',undefined,'metrics'));row.stream.append(article);}
  markdown(row.stream.querySelector('.message-body'),row.text);metrics(row.stream.querySelector('.metrics'),{usage:row.usage});if(event.kind==='model.done')row.stream.firstChild.classList.remove('streaming');
}
byId('workflow').onchange=()=>{workflowExecutable=!!byId('workflow').selectedOptions[0]?.dataset.executable;api.postMessage({type:'graph-select',id:byId('workflow').value});byId('send').disabled=!canExecute()||activeRun||sessionBusy;};
function send(){if(canExecute()&&!activeRun&&!sessionBusy&&byId('prompt').value.trim())api.postMessage({type:'send',prompt:byId('prompt').value});}
for(const type of ['new','refresh','cancel'])byId(type).onclick=()=>api.postMessage({type});
byId('sessions').onchange=()=>{refreshRename();api.postMessage({type:'select',id:byId('sessions').value});};byId('send').onclick=send;
byId('runs').onchange=()=>api.postMessage({type:'select-run',id:byId('runs').value});
byId('model').onchange=()=>api.postMessage({type:'model',id:byId('model').value});
byId('prompt').onkeydown=event=>{if(event.key==='Enter'&&!event.shiftKey){event.preventDefault();send();}};
byId('prompt').oninput=()=>{byId('prompt').style.height='auto';byId('prompt').style.height=Math.min(180,byId('prompt').scrollHeight)+'px';};
const renameDialog=byId('rename-dialog');
function refreshRename(){const selected=byId('sessions').selectedOptions[0];byId('rename').hidden=!renameCapability;byId('rename').disabled=!renameCapability||!selected?.dataset.sessionId;if(renameSnapshot&&(!renameCapability||renameSnapshot.id!==byId('sessions').value))renameDialog.close();}
byId('rename').onclick=()=>{const selected=byId('sessions').selectedOptions[0];if(!renameCapability||!selected?.dataset.sessionId)return;renameSnapshot={id:selected.value,title:selected.textContent};byId('conversation-title').value=renameSnapshot.title;byId('rename-status').textContent='';byId('rename-save').disabled=false;renameDialog.showModal();byId('conversation-title').focus();};
for(const id of ['rename-close','rename-cancel'])byId(id).onclick=()=>renameDialog.close();
renameDialog.addEventListener('close',()=>{renameSnapshot=undefined;byId('conversation-title').value='';byId('rename-save').disabled=false;if(!byId('rename').hidden)byId('rename').focus();});
byId('rename-form').onsubmit=event=>{event.preventDefault();if(!renameSnapshot||byId('rename-save').disabled)return;const title=byId('conversation-title').value;if(!title.trim())return;byId('rename-save').disabled=true;byId('rename-status').textContent='Saving…';api.postMessage({type:'rename-session',id:renameSnapshot.id,title,expected_title:renameSnapshot.title});};
const settings=byId('provider-settings');
let profileState;
function profileRoutes(){
  if(!profileState)return;const selected=profileState.profiles.find(value=>value.id===byId('provider-profile').value),routes=profileState.routes.filter(value=>value.discovery&&providerWireLabel(value.wire)&&(!selected||value.provider===selected.provider));
  const choose=byId('provider-name');choose.replaceChildren();for(const route of routes){const option=node('option',providerLabel(route.provider)+' · '+providerWireLabels[route.wire]);option.value=route.id;choose.append(option);}
  if(selected)choose.value=selected.route_id;else if(routes.some(value=>value.id==='openai.responses'))choose.value='openai.responses';
  choose.disabled=!routes.length;byId('profile-use').disabled=!selected||selected.id===profileState.active;
}
byId('provider-profile').onchange=()=>{byId('provider-key').value='';byId('settings-status').textContent='';profileRoutes();api.postMessage({type:'discardProviderKey'});};
byId('provider-name').onchange=()=>{byId('provider-key').value='';api.postMessage({type:'discardProviderKey'});};
byId('profile-use').onclick=()=>{byId('provider-key').value='';api.postMessage({type:'select-provider',id:byId('provider-profile').value});};
byId('settings').onclick=()=>{byId('provider-key').value='';settings.showModal();byId('provider-key').focus();};
byId('settings-close').onclick=()=>{api.postMessage({type:'discardProviderKey'});settings.close();};
settings.addEventListener('cancel',()=>api.postMessage({type:'discardProviderKey'}));
settings.addEventListener('close',()=>{byId('provider-key').value='';byId('settings').focus();});
byId('provider-form').onsubmit=event=>{event.preventDefault();const key=byId('provider-key').value;byId('provider-key').value='';byId('settings-save').disabled=true;byId('settings-status').textContent='Fetching models…';api.postMessage({type:'saveProviderKey',key,...(profileState?{profile:byId('provider-profile').value,route:byId('provider-name').value}:{})});};
function renderModels(models,selected){byId('model').replaceChildren();const placeholder=node('option',models.length?'Choose a model':'Open Settings to fetch models');placeholder.value='';placeholder.disabled=true;placeholder.selected=!models.some(model=>model.id===selected);byId('model').append(placeholder);for(const model of models){const option=node('option',model.id);option.value=model.id;option.selected=model.id===selected;byId('model').append(option);}byId('model').disabled=!models.length;byId('model').title=models.length?'Choose a discovered model':'Open Settings, enter your provider key and fetch models';}
window.addEventListener('message',event=>{
  if(globalThis.xMindView&&(event.source!==window||event.origin!==location.origin))return;
  const m=event.data;if(!m||typeof m.type!=='string')return;
  const scroll=byId('scroll'),follow=scroll.scrollHeight-scroll.scrollTop-scroll.clientHeight<80;
  if(m.type==='graphs'){const select=byId('workflow');select.replaceChildren();const single=node('option','Agent (default)');single.value='';select.append(single);for(const graph of m.graphs){const option=node('option',graph.id+' · '+graph.node_count+' nodes'+(graph.executable?'':' · unavailable'));option.value=graph.id;option.disabled=!graph.executable;option.dataset.executable=graph.executable?'true':'';option.selected=graph.id===m.selected;select.append(option);}if(!m.selected)select.value='';workflowExecutable=!!m.graphs.find(g=>g.id===m.selected&&g.executable);byId('workflow-picker').hidden=!m.graphs.length;byId('send').disabled=!canExecute()||activeRun||sessionBusy;}
  else if(m.type==='graph-clear')clearGraph();
  else if(m.type==='graph')graphView(m.record,m.children,m.histories);
  else if(m.type==='graph-event')graphEvent(m);
  else if(m.type==='sessions'){byId('sessions').replaceChildren();if(!m.sessions.length)byId('sessions').append(node('option','No sessions yet'));for(const session of m.sessions){const option=node('option',session.title);option.value=session.id;option.dataset.sessionId=session.id;option.selected=session.id===m.selected;byId('sessions').append(option);}refreshRename();}
  else if(m.type==='rename-result'){if(renameSnapshot?.id===m.id){byId('rename-save').disabled=false;byId('rename-status').textContent=m.text||'';if(m.success)renameDialog.close();}}
  else if(m.type==='runs'){
    sessionBusy=m.busy;byId('run-picker').hidden=!m.runs.length;byId('runs').replaceChildren();
    for(const run of m.runs){const option=node('option',run.state+' · '+run.id);option.value=run.id;option.selected=run.id===m.selected;byId('runs').append(option);}
    renderRunContext(m.runs.find(run=>run.id===m.selected));
    byId('send').disabled=!canExecute()||activeRun||sessionBusy;
  }
  else if(m.type==='reset-run'){resetLive();resetFailure();resetProcessStreams();byId('events').textContent='';renderRunContext();}
  else if(m.type==='history'||m.type==='transcript'){byId('history').replaceChildren();if(!m.preserveLive)resetLive();if(m.type==='history'){resetFailure();resetProcessStreams();byId('events').textContent='';}byId('empty').hidden=m.history.length>0||!!live||processStreams.size>0||!byId('run-failure').hidden;for(const item of m.history)entry(item.role,item.data);}
  else if(m.type==='model-list'){renderModels(m.models||[],m.model);}
  else if(m.type==='provider-profiles'){profileState=m;byId('profile-controls').hidden=false;const profiles=byId('provider-profile');profiles.replaceChildren();for(const profile of m.profiles){const option=node('option',providerLabel(profile.provider)+' · '+(profile.model||'Choose a model'));option.value=profile.id;profiles.append(option);}const add=node('option','Add profile');add.value='';profiles.append(add);profiles.value=m.active;profileRoutes();}
  else if(m.type==='provider-wire'){const label=byId('provider-mode');label.textContent=m.wire==='gemini-generate-content'?'Gemini GenerateContent':m.wire==='anthropic-messages'?'Claude Messages':providerWireLabel(m.wire)||'';label.hidden=!label.textContent;}
  else if(m.type==='settings-state'){byId('settings-status').textContent=m.text;byId('settings-save').disabled=!!m.busy;if(m.complete && settings.open)settings.close();}
  else if(m.type==='capabilities'){execution=m.execution;renameCapability=m.renameSessions===true;refreshRename();byId('send').disabled=!canExecute()||activeRun||sessionBusy;renderModels(m.models||[],m.model);if(!execution)byId('status').textContent='Backend connected · configure a model to run an agent';}
  else if(m.type==='user'){entry('user',{content:m.text});resetLive();resetFailure();resetProcessStreams();byId('prompt').value='';byId('events').textContent='';}
  else if(m.type==='draft')byId('prompt').value=m.text;
  else if(m.type==='event'){const event=m.event;byId('events').textContent+=JSON.stringify(event)+'\n';if(event.kind==='run.failed')runFailure(event.data);else if(event.kind==='process.output')processOutput(event.data);else if(event.kind==='model.text'||event.kind==='model.refusal')stream(event.data.text);else if(event.kind==='model.usage'){streamUsage=event.data;if(!live)stream('');metrics(live.querySelector('.metrics'),{usage:streamUsage});}else if(event.kind==='model.done'){if(live)live.classList.remove('streaming');}else if(event.kind==='conversation.assistant'||event.kind==='conversation.tool_turn')resetLive();}
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
  else if(m.type==='status'){byId('status').textContent=m.text;activeRun=['queued','running','paused'].includes(m.text);byId('send').disabled=!canExecute()||activeRun||sessionBusy;byId('cancel').hidden=!activeRun;}
  else if(m.type==='error'){for(const row of graphRows.values())if(row.button)row.button.disabled=false;byId('status').textContent=m.text;if(settings.open){byId('settings-status').textContent=m.text;byId('settings-save').disabled=false;}}
  if(follow)scroll.scrollTop=scroll.scrollHeight;
});api.postMessage({type:'ready'});
