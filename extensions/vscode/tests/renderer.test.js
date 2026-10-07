'use strict';
// DOM renderer contracts use labeled fixtures. No fixture is inserted into the
// interactive preview, and these tests do not claim live model execution.
const test=require('node:test'),assert=require('node:assert/strict'),fs=require('node:fs'),path=require('node:path');
const {JSDOM}=require('jsdom');const {html}=require('../webview');
function renderer(){
  const dom=new JSDOM(html('fixture-nonce',{source:'https://fixture',css:'fixture.css',marked:'marked.js',purify:'purify.js',script:'chat.js'}),{runScripts:'outside-only'}),posted=[];
  dom.window.acquireVsCodeApi=()=>({postMessage:message=>posted.push(message)});
  for(const file of ['node_modules/marked/lib/marked.umd.js','node_modules/dompurify/dist/purify.min.js','media/chat.js']) dom.window.eval(fs.readFileSync(path.join(__dirname,'..',file),'utf8'));
  return {dom,posted,send:data=>dom.window.dispatchEvent(new dom.window.MessageEvent('message',{data}))};
}
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
  const selector=r.dom.window.document.querySelector('footer #model');assert.ok(selector);assert.equal(selector.disabled,false);assert.deepEqual([...selector.options].map(option=>option.value),['synthetic-catalogue-a','synthetic-catalogue-b']);
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
