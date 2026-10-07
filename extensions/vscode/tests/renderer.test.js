'use strict';
// DOM renderer contracts use labeled fixtures. No fixture is inserted into the
// interactive preview, and these tests do not claim live model execution.
const test=require('node:test'),assert=require('node:assert/strict'),fs=require('node:fs'),path=require('node:path');
const {JSDOM}=require('jsdom');const {html}=require('../webview');
function renderer(){
  const dom=new JSDOM(html('fixture-nonce',{source:'https://fixture',css:'fixture.css',marked:'marked.js',purify:'purify.js',script:'chat.js'}),{runScripts:'outside-only'}),posted=[];
  dom.window.acquireVsCodeApi=()=>({postMessage:message=>posted.push(message)});
  dom.window.TextDecoder=TextDecoder;
  dom.window.TextEncoder=TextEncoder;
  for(const file of ['node_modules/marked/lib/marked.umd.js','node_modules/dompurify/dist/purify.min.js','media/chat.js']) dom.window.eval(fs.readFileSync(path.join(__dirname,'..',file),'utf8'));
  return {dom,posted,send:data=>dom.window.dispatchEvent(new dom.window.MessageEvent('message',{data}))};
}

const processProposalFixture=()=>({id:'fixture-command',tool:'run_process',state:'awaiting_approval',workspace_id:'fixture-root',expires_unix_ms:Date.now()+60000,
  arguments_json:JSON.stringify({profile_id:'fixture-profile',profile_revision:2,executable:'C:/fixture/tool.exe',executable_id:'opaque-fixture-backend-executable-binding',arguments:['space argument','<script>fixtureAttack()</script>','trailing\\'],workdir:'src',directory_id:'fixture-directory',timeout_ms:120000,output_limit:65536}),result_json:'{}'});
const processOutcomeFixture=()=>{
  const output='<script>fixtureAttack()</script>\u001b[31m\nfixture stdout';
  return {operation_id:'fixture-command',profile_id:'fixture-profile',pid:123,exit_code:7,termination:'exited',elapsed_ms:1500,
    stdout:{encoding:'utf-8',data:output,byte_count:90,retained_bytes:Buffer.byteLength(output)},
    stderr:{encoding:'hex',data:'ff00fe0a',byte_count:4,retained_bytes:4},truncated:true,process_tree_retired:true,independently_verified:false};
};
const processOutputFixture=(data,offset=0,channel='stdout',id='fixture-stream')=>({type:'event',event:{kind:'process.output',data:{operation_id:id,profile_id:'fixture-profile',channel,encoding:'hex',offset,retained_bytes:data.length/2,data}}});

test('provider failures remain visible through transcript refresh without invented replies or metrics',()=>{
  const r=renderer(),doc=r.dom.window.document,failure={type:'event',event:{kind:'run.failed',data:{reason:'provider_http_error',status:400,message:'<script>fixtureAttack()</script> private body'}}};
  r.send(failure);r.send(failure);r.send({type:'transcript',history:[]});
  const card=doc.getElementById('run-failure');assert.equal(card.hidden,false);assert.match(card.textContent,/HTTP 400/);assert.equal(card.querySelectorAll('h4').length,1);
  assert.equal(card.querySelector('.metrics'),null);assert.equal(doc.querySelector('#history .assistant'),null);assert.equal(doc.getElementById('empty').hidden,true);
  assert.ok(!card.textContent.includes('private body'));assert.ok(!card.textContent.includes('API key'));assert.equal(card.querySelector('script'),null);
  r.send({type:'reset-run'});assert.equal(card.hidden,true);r.send(failure);r.send({type:'history',history:[]});assert.equal(card.hidden,true);
  r.send(failure);r.send({type:'user',text:'Fixture next request'});assert.equal(card.hidden,true);
});
test('unknown failure reasons do not interpolate untrusted payloads or guess provider causes',()=>{
  const r=renderer(),doc=r.dom.window.document;r.send({type:'event',event:{kind:'run.failed',data:{reason:'<img onerror=fixtureAttack()>',status:'401'}}});
  const card=doc.getElementById('run-failure');assert.match(card.textContent,/Execution failed/);assert.ok(!card.textContent.includes('401'));assert.equal(card.querySelector('img'),null);
});
test('recorded provider identifiers render without raw error messages or unknown fields',()=>{
  const r=renderer(),doc=r.dom.window.document;
  r.send({type:'event',event:{kind:'run.failed',data:{reason:'provider_http_error',status:400,provider_error_type:'invalid_request_error',provider_error_code:'unsupported_parameter',provider_error_param:'n',message:'private-fixture-key'}}});
  const card=doc.getElementById('run-failure');assert.match(card.textContent,/Type: invalid_request_error/);assert.match(card.textContent,/Code: unsupported_parameter/);assert.match(card.textContent,/Parameter: n/);assert.ok(!card.textContent.includes('private-fixture-key'));
  r.send({type:'event',event:{kind:'run.failed',data:{reason:'provider_http_error',status:400,provider_error_type:'private-fixture-key',provider_error_code:'<script>fixtureAttack()</script>',provider_error_param:'private-fixture-key'}}});
  assert.equal(card.querySelector('details'),null);assert.ok(!card.textContent.includes('private-fixture-key'));assert.equal(card.querySelector('script'),null);
});

test('durable command output renders independent channels without executing markup or controls',()=>{
  const r=renderer(),doc=r.dom.window.document,text='<script>fixtureAttack()</script>\u001b[31m\nfixture output',initialMessages=r.posted.length;
  r.send(processOutputFixture(Buffer.from(text).toString('hex')));r.send(processOutputFixture('ff00fe',0,'stderr'));
  const card=doc.querySelector('.process-stream');assert.ok(card.textContent.includes('Captured command output · fixture-profile'));
  assert.ok(card.textContent.includes('<script>fixtureAttack()</script>\\u001b[31m'));assert.ok(card.textContent.includes('ff00fe'));assert.equal(card.querySelector('script'),null);assert.equal(r.dom.window.fixtureAttack,undefined);assert.equal(r.posted.length,initialMessages);
  assert.ok(!card.textContent.includes('Exited'));assert.ok(!card.textContent.includes('PID '));
});
test('output replay preserves fragmented Unicode and ignores only exact duplicate bytes',()=>{
  const r=renderer(),doc=r.dom.window.document;r.send(processOutputFixture('f09f'));assert.equal(doc.querySelector('.process-stream pre').textContent,'f09f');
  r.send(processOutputFixture('f09f'));r.send(processOutputFixture('8c8d',2));assert.equal(doc.querySelector('.process-stream pre').textContent,'🌍');
  assert.ok(doc.querySelector('.process-stream summary').textContent.includes('4 retained bytes'));
  r.send(processOutputFixture('4142',0));assert.ok(doc.querySelector('.process-stream').textContent.includes('Output sequence incomplete'));assert.equal(doc.querySelector('.process-stream pre').textContent,'🌍');
});
test('gaps, malformed bytes and capture overflow never invent output',()=>{
  const r=renderer(),doc=r.dom.window.document;r.send(processOutputFixture('not hex'));assert.equal(doc.querySelector('.process-stream'),null);
  r.send(processOutputFixture('41',5));assert.equal(doc.querySelector('.process-stream pre').textContent,'');assert.ok(doc.querySelector('.process-stream').textContent.includes('Output sequence incomplete'));
  r.send({type:'reset-run'});for(let i=0;i<16;i++)r.send(processOutputFixture('41'.repeat(4096),i*4096));
  assert.equal(doc.querySelector('.process-stream pre').textContent.length,65536);r.send(processOutputFixture('42',0,'stderr'));assert.ok(doc.querySelector('.process-stream').textContent.includes('Output sequence incomplete'));assert.equal(doc.querySelectorAll('.process-stream pre')[1].textContent,'');
});
test('recorded output survives transcript refresh and clears on run or conversation change',()=>{
  const r=renderer(),doc=r.dom.window.document;r.send(processOutputFixture('4142'));r.send({type:'transcript',preserveLive:true,history:[]});assert.equal(doc.querySelector('.process-stream pre').textContent,'AB');assert.equal(doc.getElementById('empty').hidden,true);
  r.send({type:'reset-run'});assert.equal(doc.querySelector('.process-stream'),null);r.send(processOutputFixture('43'));r.send({type:'history',history:[]});assert.equal(doc.querySelector('.process-stream'),null);assert.equal(doc.getElementById('empty').hidden,false);
  r.send(processOutputFixture('44'));r.send({type:'user',text:'New fixture request'});assert.equal(doc.querySelector('.process-stream'),null);
});
test('the sidebar bounds command stream cards and leaves excess output in recorded activity',()=>{
  const r=renderer(),doc=r.dom.window.document;for(let i=0;i<65;i++)r.send(processOutputFixture('41',0,'stdout','fixture-stream-'+i));assert.equal(doc.querySelectorAll('.process-stream').length,64);assert.ok(doc.getElementById('process-stream-limit').textContent.includes('recorded command results'));
  r.send({type:'reset-run'});assert.equal(doc.getElementById('process-stream-limit'),null);
});

test('command approval reviews literal arguments before sending only a decision ID',()=>{
  const r=renderer(),doc=r.dom.window.document,operation=processProposalFixture();r.send({type:'operations',operations:[operation]});
  const section=doc.querySelector('#operations .operation'),payload=JSON.parse(operation.arguments_json);
  assert.equal(section.querySelector('.process-argv').textContent,JSON.stringify([payload.executable,...payload.arguments],null,2));
  assert.match(section.textContent,/Profile fixture-profile · revision 2/);assert.match(section.textContent,/Directory: src/);assert.match(section.textContent,/Timeout 120s/);assert.match(section.textContent,/65,536 bytes/);
  assert.equal(section.querySelectorAll('script').length,0);
  assert.ok(section.textContent.includes('Executable binding'));assert.ok(section.textContent.includes('opaque-fixture-backend-executable-binding'));
  const allow=[...section.querySelectorAll('button')].find(button=>button.textContent==='Allow command');allow.click();
  assert.equal(JSON.stringify(r.posted.at(-1)),JSON.stringify({type:'decide',id:operation.id,decision:'allow'}));assert.ok([...section.querySelectorAll('button')].every(button=>button.disabled));r.dom.window.close();
});
test('approval displays bound guidance hashes and rejects malformed source metadata',()=>{
  const r=renderer(),doc=r.dom.window.document,operation=processProposalFixture(),plan=JSON.parse(operation.arguments_json);
  const source={path:'src/<script>fixtureAttack()</script>\u001bAGENTS.md',workspace_id:'fixture-root',file_id:'fixture-file',content_sha256:'a'.repeat(64),byte_count:17};
  plan.repository_guidance={version:1,directory:'src',sources:[source]};operation.arguments_json=JSON.stringify(plan);r.send({type:'operations',operations:[operation]});
  const detail=doc.querySelector('.guidance-binding');assert.ok(detail);assert.ok(detail.textContent.includes(source.content_sha256));assert.ok(detail.textContent.includes('17 bytes'));assert.ok(detail.textContent.includes('\\u001b'));assert.equal(detail.querySelector('script'),null);assert.equal(r.dom.window.fixtureAttack,undefined);
  let allow=[...doc.querySelectorAll('#operations button')].find(x=>x.textContent==='Allow command');assert.equal(allow.disabled,false);assert.ok(detail.textContent.includes('a new approval'));
  for(const bad of [{...source,content_sha256:'not-a-hash'},{...source,byte_count:16385},{...source,file_id:''},null]){plan.repository_guidance.sources=[bad];operation.arguments_json=JSON.stringify(plan);r.send({type:'operations',operations:[operation]});allow=[...doc.querySelectorAll('#operations button')].find(x=>x.textContent==='Allow command');assert.equal(allow.disabled,true);assert.ok(doc.querySelector('#operations').textContent.includes('binding is malformed'));}
  plan.repository_guidance.sources=[];operation.arguments_json=JSON.stringify(plan);r.send({type:'operations',operations:[operation]});assert.ok(doc.querySelector('.guidance-binding').textContent.includes('No AGENTS.md sources'));allow=[...doc.querySelectorAll('#operations button')].find(x=>x.textContent==='Allow command');assert.equal(allow.disabled,false);r.dom.window.close();
});

test('malformed or expired command proposals cannot be allowed',()=>{
  const r=renderer(),doc=r.dom.window.document,operation=processProposalFixture();
  r.send({type:'operations',operations:[{...operation,arguments_json:'{"profile_id":"fixture-profile"}'}]});
  let buttons=[...doc.querySelectorAll('#operations button')];assert.ok(buttons.find(button=>button.textContent==='Allow command').disabled);assert.ok(!buttons.find(button=>button.textContent==='Deny').disabled);
  assert.match(doc.querySelector('#operations').textContent,/details are unavailable/);
  for(const binding of [undefined,'',null,'contains\0NUL','x'.repeat(513)]){
    const plan=JSON.parse(operation.arguments_json);plan.executable_id=binding;r.send({type:'operations',operations:[{...operation,arguments_json:JSON.stringify(plan)}]});
    buttons=[...doc.querySelectorAll('#operations button')];assert.ok(buttons.find(button=>button.textContent==='Allow command').disabled);assert.ok(!buttons.find(button=>button.textContent==='Deny').disabled);
  }
  r.send({type:'operations',operations:[{...operation,expires_unix_ms:Date.now()-1000}]});buttons=[...doc.querySelectorAll('#operations button')];assert.ok(buttons.every(button=>button.disabled));r.dom.window.close();
});

test('command results preserve separate escaped output, hex bytes and actual exit metrics',()=>{
  const r=renderer(),doc=r.dom.window.document,operation=processProposalFixture(),result=processOutcomeFixture();
  r.send({type:'operations',operations:[{...operation,state:'succeeded',result_json:JSON.stringify(result)}]});const view=doc.querySelector('.process-result');
  assert.match(view.textContent,/Exited · exit 7/);assert.match(view.textContent,/PID 123/);assert.match(view.textContent,/1.50s/);
  assert.equal(view.querySelector('.process-stdout').textContent,'<script>fixtureAttack()</script>\\u001b[31m\nfixture stdout');assert.equal(view.querySelector('.process-stderr').textContent,'ff00fe0a');
  assert.match(view.textContent,/stderr · 4 bytes · 4 retained · hex/);assert.match(view.textContent,/capture limit/);assert.match(view.textContent,/not been independently verified/);
  assert.equal(view.querySelectorAll('script').length,0);assert.equal(doc.querySelectorAll('#operations button').length,0);
  r.send({type:'history',history:[{role:'tool',data:{content:JSON.stringify(result),tool_call_id:'fixture-call'}}]});
  assert.ok(doc.querySelector('#history .process-result'));assert.match(doc.querySelector('#history summary').textContent,/Command result/);r.dom.window.close();
});

test('uncertain commands remain blocked and malformed outcomes retain raw evidence',()=>{
  const r=renderer(),doc=r.dom.window.document,operation=processProposalFixture(),result={...processOutcomeFixture(),termination:'cancelled',exit_code:1};
  r.send({type:'operations',operations:[{...operation,state:'uncertain',result_json:JSON.stringify(result)}]});
  assert.match(doc.querySelector('#operations').textContent,/Cancelled · exit 1/);assert.match(doc.querySelector('#operations').textContent,/through this profile are blocked/);assert.match(doc.querySelector('#operations').textContent,/will not retry/);assert.equal(doc.querySelectorAll('#operations button').length,0);
  const malformed={...result,stderr:{encoding:'hex',data:'not valid hex'}};r.send({type:'history',history:[{role:'tool',data:{content:JSON.stringify(malformed)}}]});
  assert.equal(doc.querySelectorAll('#history .process-result').length,0);assert.equal(doc.querySelector('#history pre').textContent,JSON.stringify(malformed));r.dom.window.close();
});

test('command output metrics require exact encoded retained bytes and safe drained counts',()=>{
  const r=renderer(),doc=r.dom.window.document,result=processOutcomeFixture();result.stdout={encoding:'utf-8',data:'🌍\0',byte_count:5,retained_bytes:5};
  r.send({type:'history',history:[{role:'tool',data:{content:JSON.stringify(result)}}]});assert.equal(doc.querySelector('.process-stdout').textContent,'🌍\\u0000');assert.ok(doc.querySelector('.process-result').textContent.includes('5 bytes · 5 retained'));
  for(const patch of [{retained_bytes:3},{byte_count:4},{byte_count:-1},{byte_count:undefined},{retained_bytes:undefined},{byte_count:Number.MAX_SAFE_INTEGER+1}]){
    const malformed={...result,stdout:{...result.stdout,...patch}};r.send({type:'history',history:[{role:'tool',data:{content:JSON.stringify(malformed)}}]});
    assert.equal(doc.querySelector('.process-result'),null);assert.equal(doc.querySelector('#history pre').textContent,JSON.stringify(malformed));
  }
  const malformed={...result,stderr:{...result.stderr,retained_bytes:3}};r.send({type:'history',history:[{role:'tool',data:{content:JSON.stringify(malformed)}}]});assert.equal(doc.querySelector('.process-result'),null);assert.equal(doc.querySelector('#history pre').textContent,JSON.stringify(malformed));r.dom.window.close();
});
test('new-file review distinguishes absence, previews exact content and never retries uncertainty',()=>{
  const r=renderer(),doc=r.dom.window.document;const operation={id:'fixture-create',tool:'create_file',state:'awaiting_approval',workspace_id:'fixture-root',expires_unix_ms:Date.now()+60000,arguments_json:JSON.stringify({path:'new.cpp',parent_id:'fixture-parent',before_exists:false,before_content:'',after_content:'actual proposed source\n'}),result_json:'{}'};
  r.send({type:'operations',operations:[operation]});assert.equal(doc.querySelector('#operations .after').textContent,'actual proposed source\n');assert.match(doc.querySelector('#operations').textContent,/New file/);
  const controls=[...doc.querySelectorAll('#operations button')];controls.find(button=>button.textContent==='Compare changes').click();assert.equal(r.posted.at(-1).type,'review');assert.equal(r.posted.at(-1).id,operation.id);
  controls.find(button=>button.textContent==='Allow creation').click();assert.equal(r.posted.at(-1).type,'decide');assert.equal(r.posted.at(-1).decision,'allow');
  r.send({type:'operations',operations:[{...operation,state:'uncertain'}]});assert.equal(doc.querySelectorAll('#operations button').length,0);assert.match(doc.querySelector('#operations').textContent,/Creation is uncertain/);r.dom.window.close();
});

test('external MCP approval names the server and tool and uncertainty never offers replay',()=>{
  const r=renderer(),doc=r.dom.window.document;
  const operation={id:'fixture-mcp',tool:'mcp_tool',state:'awaiting_approval',workspace_id:'fixture-root',expires_unix_ms:Date.now()+60000,arguments_json:JSON.stringify({server_config_id:'<fixture-server>',peer_tool:'fixture.write',config_revision:2,arguments_json:'{"body":"fixture"}'}),result_json:'{}'};
  r.send({type:'operations',operations:[operation]});
  assert.match(doc.querySelector('#operations').textContent,/External tool: <fixture-server> \/ fixture.write/);
  assert.equal(doc.querySelectorAll('#operations fixture-server').length,0);
  const allow=[...doc.querySelectorAll('#operations button')].find(button=>button.textContent==='Allow tool');assert.ok(allow);allow.click();
  assert.equal(JSON.stringify(r.posted.at(-1)),JSON.stringify({type:'decide',id:operation.id,decision:'allow'}));
  r.send({type:'operations',operations:[{...operation,state:'uncertain'}]});
  assert.equal(doc.querySelectorAll('#operations button').length,0);assert.match(doc.querySelector('#operations').textContent,/on this configured server remain blocked/);
  r.send({type:'operations',operations:[{...operation,state:'succeeded'}]});assert.match(doc.querySelector('#operations').textContent,/not independently verified/);r.dom.window.close();
});
test('run detail selector preserves conversation history and blocks submission while another run is active',()=>{
  const r=renderer(),doc=r.dom.window.document;
  r.send({type:'capabilities',execution:true,models:[{id:'fixture-model'}],model:'fixture-model'});
  r.send({type:'history',history:[{role:'assistant',data:{content:'Existing fixture history'}}]});
  r.send({type:'runs',runs:[{id:'older',state:'failed'},{id:'latest',state:'running'}],selected:'older',busy:true});
  r.send({type:'status',text:'failed'});
  r.send({type:'capabilities',execution:true,models:[{id:'fixture-model'}],model:'fixture-model'});
  assert.equal(doc.querySelector('#run-picker').hidden,false);assert.equal(doc.querySelector('#runs').value,'older');assert.equal(doc.querySelector('#send').disabled,true);
  doc.querySelector('#prompt').value='Must not submit while another run is active';doc.querySelector('#prompt').dispatchEvent(new r.dom.window.KeyboardEvent('keydown',{key:'Enter'}));assert.ok(!r.posted.some(message=>message.type==='send'));
  doc.querySelector('#runs').value='latest';doc.querySelector('#runs').dispatchEvent(new r.dom.window.Event('change'));
  assert.equal(JSON.stringify(r.posted.at(-1)),JSON.stringify({type:'select-run',id:'latest'}));
  r.send({type:'event',event:{kind:'model.text',data:{text:'Selected fixture stream'}}});r.send({type:'reset-run'});
  assert.equal(doc.querySelector('#live').childElementCount,0);assert.equal(doc.querySelector('#history .message-body').textContent.trim(),'Existing fixture history');
  r.send({type:'runs',runs:[{id:'older',state:'failed'},{id:'latest',state:'completed'}],selected:'older',busy:false});assert.equal(doc.querySelector('#send').disabled,false);
  r.dom.window.close();
});
test('uncertain edit inspection renders escaped observations and never offers an effect approval',()=>{
  const r=renderer();const operation={id:'fixture-uncertain',tool:'replace_file',state:'uncertain',workspace_id:'fixture-root',expires_unix_ms:Date.now(),arguments_json:'{}',result_json:'{}'};
  r.send({type:'operations',operations:[operation]});
  const doc=r.dom.window.document,buttons=[...doc.querySelectorAll('#operations button')];assert.equal(buttons.length,1);assert.equal(buttons[0].textContent,'Inspect actual file');buttons[0].click();
  assert.equal(JSON.stringify(r.posted.at(-1)),JSON.stringify({type:'inspect-edit',id:operation.id}));
  const inspection={match:'after',same_file:true,observed_unix_ms:Date.now(),observed:{path:'<script>fixtureAttack()</script>',size:3,content_sha256:'b'.repeat(64)}};
  r.send({type:'edit-inspection',id:operation.id,inspection});
  assert.match(doc.querySelector('.edit-inspection').textContent,/Matches recorded after state/);
  assert.match(doc.querySelector('.edit-inspection').textContent,/outcome remains uncertain/);
  assert.match(doc.querySelector('.edit-inspection pre').textContent,/<script>fixtureAttack/);assert.equal(doc.querySelectorAll('#operations script').length,0);
  r.send({type:'operations',operations:[]});r.send({type:'edit-inspection',id:operation.id,inspection});assert.equal(doc.querySelectorAll('.edit-inspection').length,0);r.dom.window.close();
});
test('history renders Markdown/code, copies plain code and shows exact provider usage',()=>{
  const r=renderer();r.send({type:'history',history:[{role:'assistant',data:{content:'## Fixture heading\n\n**Fixture bold**\n\n```js\nx < y\n```',model:'synthetic-renderer-fixture',usage:{prompt_tokens:12,completion_tokens:6,total_tokens:18},elapsed_ms:1500}}]});
  const doc=r.dom.window.document;assert.equal(doc.querySelector('.markdown h2').textContent,'Fixture heading');assert.equal(doc.querySelector('.markdown strong').textContent,'Fixture bold');
  assert.match(doc.querySelector('.metrics').textContent,/Input 12Output 6Total 18/);assert.match(doc.querySelector('.metrics').textContent,/1.50s/);
  doc.querySelector('.copy-code').click();assert.equal(r.posted.at(-1).type,'copy');assert.equal(r.posted.at(-1).text,'x < y\n');r.dom.window.close();
});
test('unknown historical usage stays unavailable and malicious Markdown is sanitized',()=>{
  const r=renderer();r.send({type:'history',history:[{role:'assistant',data:{content:'<script>fixtureAttack()</script><img src="https://fixture.invalid" onerror="fixtureAttack()"><iframe src="https://fixture.invalid"></iframe>[fixture](javascript:fixtureAttack())'}}]});
  const card=r.dom.window.document.querySelector('.message');assert.equal(card.querySelectorAll('script,iframe,[onerror]').length,0);assert.equal(card.querySelectorAll('a[href^="javascript:"]').length,0);
  assert.equal(card.querySelector('.metrics').textContent,'Input —Output —Total —');r.dom.window.close();
});
test('live token badges use model usage events and reset when the persisted transcript arrives',()=>{
  const r=renderer();r.send({type:'event',event:{kind:'model.text',data:{text:'Synthetic renderer fixture'}}});r.send({type:'event',event:{kind:'model.usage',data:{prompt_tokens:8,completion_tokens:0,total_tokens:8}}});
  assert.equal(r.dom.window.document.querySelector('#live .metrics').textContent,'Input 8Output 0Total 8');
  r.send({type:'transcript',history:[{role:'assistant',data:{content:'Synthetic renderer fixture',usage:{prompt_tokens:8,completion_tokens:0,total_tokens:8}}}]});
  assert.equal(r.dom.window.document.querySelector('#live').childElementCount,0);assert.equal(r.dom.window.document.querySelectorAll('#history .message').length,1);r.dom.window.close();
});
test('tool-turn checkpoint starts a new live response and history refresh preserves its received prefix',()=>{
  const r=renderer();r.send({type:'event',event:{kind:'model.text',data:{text:'First fixture response'}}});r.send({type:'event',event:{kind:'conversation.tool_turn',data:{}}});
  r.send({type:'event',event:{kind:'model.text',data:{text:'Second fixture prefix'}}});r.send({type:'transcript',preserveLive:true,history:[{role:'assistant',data:{content:'First fixture response'}}]});
  assert.equal(r.dom.window.document.querySelector('#live .message-body').textContent.trim(),'Second fixture prefix');assert.equal(r.dom.window.document.querySelectorAll('#history .message').length,1);r.dom.window.close();
});
test('bottom model selector exposes only backend-advertised IDs and sends the selected ID',()=>{
  const r=renderer();r.send({type:'capabilities',execution:true,model:'synthetic-catalogue-a',models:[{id:'synthetic-catalogue-a'},{id:'synthetic-catalogue-b'}]});
  const selector=r.dom.window.document.querySelector('footer #model');assert.ok(selector);assert.equal(selector.disabled,false);assert.deepEqual([...selector.options].map(option=>option.value),['','synthetic-catalogue-a','synthetic-catalogue-b']);
  selector.value='synthetic-catalogue-b';selector.dispatchEvent(new r.dom.window.Event('change'));assert.equal(r.posted.at(-1).type,'model');assert.equal(r.posted.at(-1).id,'synthetic-catalogue-b');r.dom.window.close();
});
test('provider cache/reasoning counts and backend first-token time render without derived estimates',()=>{
  const r=renderer();r.send({type:'history',history:[{role:'assistant',data:{content:'Synthetic metrics fixture',usage:{prompt_tokens:12,completion_tokens:6,total_tokens:18,prompt_tokens_details:{cached_tokens:4},completion_tokens_details:{reasoning_tokens:2}},first_token_ms:200,elapsed_ms:1250}}]});
  const text=r.dom.window.document.querySelector('.metrics').textContent;assert.match(text,/Cached 4/);assert.match(text,/Reasoning 2/);assert.match(text,/First token 0.20s/);assert.match(text,/1.25s/);r.dom.window.close();
});

test('pending file comparison sends only an operation ID and remains separate from approval',()=>{
  const r=renderer();r.send({type:'operations',operations:[{id:'fixture-edit',tool:'replace_file',state:'awaiting_approval',workspace_id:'fixture-root',expires_unix_ms:Date.now()+60000,arguments_json:JSON.stringify({path:'file.cpp',before_content:'old',after_content:'new'})}]});
  const buttons=[...r.dom.window.document.querySelectorAll('#operations button')];
  const compare=buttons.find(button=>button.textContent==='Compare changes');assert.ok(compare);compare.click();
  assert.equal(JSON.stringify(r.posted.at(-1)),JSON.stringify({type:'review',id:'fixture-edit'}));
  assert.ok(!buttons.find(button=>button.textContent==='Allow edit').disabled);r.dom.window.close();
});

test('top-right settings dialog clears key input and leaves model selection in the footer',()=>{
  const r=renderer(),doc=r.dom.window.document,dialog=doc.getElementById('provider-settings');
  // jsdom has no dialog implementation; emulate only these native DOM methods.
  dialog.showModal=()=>{dialog.open=true;};dialog.close=()=>{dialog.open=false;dialog.dispatchEvent(new r.dom.window.Event('close'));};
  assert.ok(doc.querySelector('header #settings'));assert.equal(doc.querySelector('footer #configureModel'),null);
  doc.getElementById('settings').click();assert.equal(dialog.open,true);assert.equal(doc.getElementById('provider-key').type,'password');doc.getElementById('provider-key').value='synthetic-ui-key';
  doc.getElementById('provider-form').dispatchEvent(new r.dom.window.Event('submit',{cancelable:true}));assert.equal(r.posted.at(-1).type,'saveProviderKey');assert.equal(r.posted.at(-1).key,'synthetic-ui-key');assert.equal(doc.getElementById('provider-key').value,'');
  r.send({type:'model-list',models:[{id:'fixture-returned-model'}]});r.send({type:'settings-state',complete:true,busy:false,text:'Models fetched'});assert.equal(dialog.open,false);assert.equal(doc.getElementById('model').disabled,false);assert.equal(doc.getElementById('send').disabled,true);assert.equal(r.posted.filter(m=>m.type==='model').length,0);
  const model=doc.getElementById('model');model.value='fixture-returned-model';model.dispatchEvent(new r.dom.window.Event('change'));assert.equal(r.posted.at(-1).id,'fixture-returned-model');r.dom.window.close();
});
function graphRenderFixture(){return {type:'graph',record:{run:{id:'fixture-root',state:'paused'},graph_id:'fixture.flow',graph_revision:2,checkpoint_revision:4,spec:{nodes:[{id:'answer',type:'human',prompt:'Choose input <script>fixtureAttack()</script>'},{id:'worker',type:'agent'}]},checkpoint:{nodes:[{id:'answer',state:'waiting_human'},{id:'worker',state:'completed'}]}},children:[{id:'child-worker',node_id:'worker',state:'completed'}],histories:{'child-worker':[{role:'assistant',data:{content:'**Synthetic child response**',model:'fixture-model',usage:{prompt_tokens:12,completion_tokens:7,total_tokens:19},elapsed_ms:1400}}]}};}
test('graph human form preserves a draft across checkpoints and sends raw input with the displayed revision',()=>{
 const r=renderer(),doc=r.dom.window.document,fixture=graphRenderFixture();r.send(fixture);const input=doc.querySelector('.graph-input textarea');input.value='{"path":"left.txt","path":"right.txt"}';fixture.record.checkpoint_revision=5;r.send(fixture);assert.equal(doc.querySelector('.graph-input textarea'),input);assert.equal(input.value,'{"path":"left.txt","path":"right.txt"}');
 doc.querySelector('.graph-input').dispatchEvent(new r.dom.window.Event('submit',{cancelable:true}));assert.equal(r.posted.at(-1).type,'graph-input');assert.equal(r.posted.at(-1).root,'fixture-root');assert.equal(r.posted.at(-1).node,'answer');assert.equal(r.posted.at(-1).revision,5);assert.equal(r.posted.at(-1).input_json,input.value);assert.equal(doc.querySelector('#graph-view script'),null);
 fixture.record.run.state='completed';fixture.record.checkpoint.nodes[0]={id:'answer',state:'completed',output:{accepted:true}};r.send(fixture);assert.equal(doc.querySelector('.graph-input'),null);r.send({type:'graph-clear'});assert.equal(doc.getElementById('graph-view').hidden,true);r.dom.window.close();
});
test('graph child responses retain actual supplied per-response metrics independently of the root transcript',()=>{
 const r=renderer(),doc=r.dom.window.document;r.send(graphRenderFixture());const worker=[...doc.querySelectorAll('.graph-node')].find(row=>row.querySelector('h4').textContent.startsWith('worker ·')),child=worker.querySelector('.graph-responses');assert.ok(child.textContent.includes('Synthetic child response'));assert.ok(child.textContent.includes('Input 12'));assert.ok(child.textContent.includes('Output 7'));assert.equal(doc.getElementById('history').children.length,0);assert.equal(doc.getElementById('live').children.length,0);
 r.send({type:'graph-event',node_id:'worker',event:{run_id:'child-worker',kind:'model.text',data:{text:'Synthetic next turn'}}});r.send({type:'graph-event',node_id:'worker',event:{run_id:'child-worker',kind:'model.usage',data:{prompt_tokens:9,completion_tokens:3,total_tokens:12}}});assert.ok(worker.querySelector('.graph-live').textContent.includes('Input 9'));assert.equal(doc.getElementById('live').children.length,0);r.dom.window.close();
});
test('footer workflow chooser enables a real advertised tool graph on a model-free backend',()=>{
 const r=renderer(),doc=r.dom.window.document;r.send({type:'capabilities',execution:false,models:[]});r.send({type:'graphs',graphs:[{id:'fixture.read',revision:1,node_count:1,executable:true},{id:'fixture.unavailable',revision:1,node_count:1,executable:false}],selected:'fixture.read'});assert.equal(doc.querySelector('footer #workflow').value,'fixture.read');assert.equal(doc.getElementById('send').disabled,false);assert.equal(doc.querySelector('#workflow option[value="fixture.unavailable"]').disabled,true);doc.getElementById('prompt').value='Actual task intent';doc.getElementById('send').click();assert.equal(r.posted.at(-1).type,'send');assert.equal(r.posted.at(-1).prompt,'Actual task intent');r.dom.window.close();
});
test('graph join summary keeps full observed outputs expandable without fabricated aggregate metrics',()=>{
 const r=renderer(),doc=r.dom.window.document;r.send({type:'transcript',history:[{role:'assistant',data:{source:'graph_join',graph_id:'fixture.read',content:'Raw joined JSON text',nodes:[{id:'read',state:'completed',output:{content:'Observed fixture bytes'}}]}}]});const card=doc.querySelector('#history .message');assert.ok(card.textContent.includes('Completed graph fixture.read.'));assert.equal(card.querySelector('.metrics'),null);assert.equal(card.querySelector('details').open,false);assert.ok(card.querySelector('pre').textContent.includes('Observed fixture bytes'));assert.equal(card.querySelector('.markdown'),null);r.dom.window.close();
});
