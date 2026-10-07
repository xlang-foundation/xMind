'use strict';
const api=acquireVsCodeApi(),byId=id=>document.getElementById(id);
let execution=false,activeRun=false,sessionBusy=false,live,streamText='',streamUsage=null;
const operationSections=new Map();
const node=(tag,text,cls)=>{const el=document.createElement(tag);if(text!==undefined)el.textContent=text;if(cls)el.className=cls;return el;};
function markdown(el,text){
  el.innerHTML=DOMPurify.sanitize(marked.parse(text||'',{gfm:true,breaks:false}),{FORBID_TAGS:['style','iframe','form','input','button'],FORBID_ATTR:['style'],ALLOW_DATA_ATTR:false});
  for(const anchor of el.querySelectorAll('a')){anchor.addEventListener('click',event=>{event.preventDefault();api.postMessage({type:'openLink',url:anchor.getAttribute('href')});});}
  for(const block of el.querySelectorAll('pre')){const button=node('button','Copy code','copy-code');button.onclick=()=>api.postMessage({type:'copy',text:block.querySelector('code')?.textContent||block.textContent});block.after(button);}
}
const count=value=>Number.isSafeInteger(value)&&value>=0?value.toLocaleString():'—';
function metrics(el,data={}){const usage=data.usage||{};el.replaceChildren();for(const text of ['Input '+count(usage.prompt_tokens),'Output '+count(usage.completion_tokens),'Total '+count(usage.total_tokens)])el.append(node('span',text));if(Number.isSafeInteger(usage.prompt_tokens_details?.cached_tokens))el.append(node('span','Cached '+count(usage.prompt_tokens_details.cached_tokens)));if(Number.isSafeInteger(usage.completion_tokens_details?.reasoning_tokens))el.append(node('span','Reasoning '+count(usage.completion_tokens_details.reasoning_tokens)));if(data.model)el.append(node('span',data.model));if(Number.isFinite(data.first_token_ms)&&data.first_token_ms>=0)el.append(node('span','First token '+(data.first_token_ms/1000).toFixed(2)+'s'));if(Number.isFinite(data.elapsed_ms)&&data.elapsed_ms>=0)el.append(node('span',(data.elapsed_ms/1000).toFixed(2)+'s'));el.title='Provider-reported tokens and backend-measured timings. A dash means unavailable; token counts are never estimated.';}
function entry(role,data){
  byId('empty').hidden=true;
  const card=node('article',undefined,'message '+role);card.append(node('h4',role==='assistant'?'xMind':role==='user'?'You':role));
  if(role==='tool'){const detail=node('details',undefined,'tool');detail.append(node('summary','Tool result'),node('pre',data.content||JSON.stringify(data,null,2)));card.append(detail);}
  else {const content=node('div',undefined,'message-body markdown');markdown(content,data.content||data.refusal||'');card.append(content);if(data.tool_calls?.length){const detail=node('details',undefined,'tool');detail.append(node('summary',data.tool_calls.length+' tool request(s)'),node('pre',JSON.stringify(data.tool_calls,null,2)));card.append(detail);}if(role==='assistant'){const info=node('div',undefined,'metrics');metrics(info,data);card.append(info);}}
  byId('history').append(card);return card;
}
function resetLive(){byId('live').replaceChildren();live=undefined;streamText='';streamUsage=null;}
function stream(text){if(!live){live=node('article',undefined,'message assistant streaming');live.append(node('h4','xMind · responding'),node('div',undefined,'message-body markdown'),node('div',undefined,'metrics'));byId('live').append(live);byId('empty').hidden=true;}streamText+=text;markdown(live.querySelector('.message-body'),streamText);metrics(live.querySelector('.metrics'),{usage:streamUsage});}
function operations(items){byId('operations').replaceChildren();for(const item of items){const section=node('section',undefined,'operation');section.append(node('h4',item.tool+' · '+item.state));const meta=node('details');meta.append(node('summary','Operation '+item.id),node('pre','Workspace: '+item.workspace_id+'\nExpires: '+new Date(item.expires_unix_ms).toISOString()+'\nController: '+(item.decision_actor||'Awaiting decision')),node('pre',item.arguments_json));section.append(meta);if(item.tool==='replace_file'){try{const plan=JSON.parse(item.arguments_json);section.append(node('strong',plan.path));for(const [label,key] of [['Before','before_content'],['After','after_content']]){section.append(node('div',label),node('pre',plan[key],label.toLowerCase()));}}catch{}}if(item.state==='awaiting_approval'){for(const decision of ['allow','deny']){const button=node('button',decision==='allow'?'Allow edit':'Deny',decision==='allow'?'primary':'');button.disabled=Date.now()>=item.expires_unix_ms;button.onclick=()=>{for(const control of section.querySelectorAll('button'))control.disabled=true;api.postMessage({type:'decide',id:item.id,decision});};section.append(button);}}else{const detail=node('details');detail.append(node('summary','Outcome'),node('pre',item.result_json));section.append(detail);}byId('operations').append(section);}}
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
  else if(m.type==='reset-run'){resetLive();byId('events').textContent='';}
  else if(m.type==='history'||m.type==='transcript'){byId('history').replaceChildren();if(!m.preserveLive)resetLive();byId('empty').hidden=m.history.length>0||!!live;for(const item of m.history)entry(item.role,item.data);if(m.type==='history')byId('events').textContent='';}
  else if(m.type==='capabilities'){execution=m.execution;byId('send').disabled=!execution||activeRun||sessionBusy;byId('model').replaceChildren();for(const model of m.models||[]){const option=node('option',model.id);option.value=model.id;option.selected=model.id===m.model;byId('model').append(option);}if(!byId('model').options.length)byId('model').append(node('option',execution?'Backend model · identity unavailable':'No model configured'));byId('model').disabled=!execution||!(m.models||[]).length;if(!execution)byId('status').textContent='Backend connected · configure a model to run an agent';}
  else if(m.type==='user'){entry('user',{content:m.text});resetLive();byId('prompt').value='';byId('events').textContent='';}
  else if(m.type==='draft')byId('prompt').value=m.text;
  else if(m.type==='event'){const event=m.event;byId('events').textContent+=JSON.stringify(event)+'\n';if(event.kind==='model.text'||event.kind==='model.refusal')stream(event.data.text);else if(event.kind==='model.usage'){streamUsage=event.data;if(!live)stream('');metrics(live.querySelector('.metrics'),{usage:streamUsage});}else if(event.kind==='model.done'){if(live)live.classList.remove('streaming');}else if(event.kind==='conversation.assistant'||event.kind==='conversation.tool_turn')resetLive();}
  else if(m.type==='operations'){
    operationSections.clear();
    operations(m.operations);
    for(const [index,item] of m.operations.entries())if(item.tool==='replace_file'&&item.state==='uncertain'){
      const section=byId('operations').children[index];operationSections.set(item.id,section);
      section.append(node('p','This edit is uncertain. Further edits in this workspace remain blocked.','inspection-note'));
      const button=node('button','Inspect actual file');button.onclick=()=>api.postMessage({type:'inspect-edit',id:item.id});section.append(button);
    }
    for(const [index,item] of m.operations.entries())if(item.tool==='replace_file'&&item.state==='awaiting_approval'){
      const button=node('button','Compare changes');button.disabled=Date.now()>=item.expires_unix_ms;
      button.onclick=()=>api.postMessage({type:'review',id:item.id});
      byId('operations').children[index].append(button);
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
