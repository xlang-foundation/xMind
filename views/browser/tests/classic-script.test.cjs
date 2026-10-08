'use strict';
// Real product scripts/HTML in one classic-script realm. Backend replies and
// retained histories are explicitly synthetic; no native/live state is used.
const test=require('node:test'),assert=require('node:assert/strict');
const fs=require('node:fs'),path=require('node:path'),vm=require('node:vm');
const {JSDOM}=require('../../../extensions/vscode/node_modules/jsdom');
const {browserHtml}=require('../page.cjs');
const root=path.resolve(__dirname,'../../..');
const sources={
  '/ui/client.js':'extensions/vscode/client.js',
  '/ui/browser.js':'views/browser/browser.js',
  '/ui/marked.js':'extensions/vscode/node_modules/marked/lib/marked.umd.js',
  '/ui/purify.js':'extensions/vscode/node_modules/dompurify/dist/purify.min.js',
  '/ui/chat.js':'extensions/vscode/media/chat.js'
};
test('production classic scripts share a realm without collisions and restore owned history through the browser bridge',async()=>{
  const dom=new JSDOM(browserHtml(),{url:'http://127.0.0.1:8765/ui/',runScripts:'outside-only'});
  const window=dom.window,document=window.document,requests=[];
  const parent={id:'fixture-parent',session_id:'fixture-session',parent_id:'',graph_root:false,state:'completed'};
  const child={run:{id:'fixture-leaf',session_id:parent.session_id,parent_id:parent.id,graph_root:false,state:'completed'},kind:'delegated_leaf',batch_id:'fixture-batch',task_id:'inspect',preset_id:'workspace.inspect',preset_revision:1};
  const parentHistory=[{role:'assistant',data:{content:'Synthetic retained parent answer',model:'fixture-model',usage:{prompt_tokens:13,completion_tokens:5}}}];
  const childHistory=[{role:'assistant',data:{content:'Synthetic retained child finding',model:'fixture-model',usage:{prompt_tokens:7,completion_tokens:3}}}];
  const routes={
    '/ui/session':{},
    '/v1/health':{status:'ok',agent_execution:true,owned_child_observation:true},
    '/v1/models':{models:[{id:'fixture-model'}],default_model:'fixture-model'},
    '/v1/graphs':{graphs:[]},
    '/v1/sessions':[{id:parent.session_id,title:'Synthetic retained conversation'}],
    ['/v1/sessions/'+parent.session_id+'/history']:parentHistory,
    ['/v1/sessions/'+parent.session_id+'/runs']:[parent],
    ['/v1/runs/'+parent.id]:parent,
    ['/v1/runs/'+parent.id+'/children']:[child],
    ['/v1/runs/'+parent.id+'/children/'+child.run.id+'/history']:childHistory,
    ['/v1/runs/'+parent.id+'/operations']:[],
    ['/v1/runs/'+child.run.id+'/operations']:[],
    '/v1/provider/configuration':{provider:'openai',wire:'responses',revision:0,configured:false,model:''}
  };
  window.AbortController=AbortController;window.AbortSignal=AbortSignal;
  window.TextDecoder=TextDecoder;window.TextEncoder=TextEncoder;
  for(const dialog of document.querySelectorAll('dialog')){
    dialog.showModal=()=>{dialog.open=true;};
    dialog.close=()=>{dialog.open=false;dialog.dispatchEvent(new window.Event('close'));};
  }
  window.sessionStorage.setItem('xmind.view.selection',JSON.stringify({session:parent.session_id,run:parent.id,model:'fixture-model'}));
  window.fetch=async(input,options={})=>{
    const url=new URL(input,window.location.origin);assert.equal(url.origin,window.location.origin,'Fixture rejects any provider/native network destination');
    const route=url.pathname+url.search;requests.push({route,method:options.method||'GET',credentials:options.credentials,headers:options.headers});
    if(url.pathname.endsWith('/tree-events')){assert.equal(url.searchParams.get('after'),'0');return {ok:true,status:200,json:async()=>[]};}
    if(route==='/v1/provider/profiles')return {ok:false,status:404,json:async()=>({detail:'Synthetic older profile metadata unavailable'})};
    assert.ok(Object.hasOwn(routes,route),'Unexpected classic-script fixture route: '+route);
    return {ok:true,status:200,json:async()=>structuredClone(routes[route])};
  };
  try{
    const scripts=[...document.querySelectorAll('script')];
    assert.deepEqual(scripts.map(script=>script.getAttribute('src')),Object.keys(sources),'Execute the production emitted script order, not separate CommonJS modules');
    for(const script of scripts){
      assert.notEqual(script.type,'module','The product page uses classic script globals');
      const route=script.getAttribute('src');
      // vm.Script persists global lexical declarations, unlike isolated strict
      // eval/CommonJS loading which missed the original duplicate declaration.
      new vm.Script(fs.readFileSync(path.join(root,sources[route]),'utf8'),{filename:route}).runInContext(dom.getInternalVMContext());
      if(route==='/ui/browser.js'){
        assert.equal(typeof window.xMindView?.postMessage,'function','Install the real browser bridge before the renderer');
        assert.equal(typeof window.acquireVsCodeApi,'undefined','The webpage has no VS Code host fallback');
      }
    }
    for(let attempts=0;attempts<100&&!document.getElementById('workspace-info').textContent.startsWith('Connected');++attempts)await new Promise(resolve=>setImmediate(resolve));
    assert.match(document.getElementById('workspace-info').textContent,/Connected to the native xMind runtime/);
    assert.equal(document.getElementById('connection').open,false);
    assert.equal(document.getElementById('sessions').value,parent.session_id);
    assert.equal(document.querySelector('footer #model').value,'fixture-model');
    assert.match(document.getElementById('history').textContent,/Synthetic retained parent answer/);
    assert.match(document.querySelector('#history .metrics').textContent,/Input 13Output 5Total —/);
    assert.match(document.getElementById('owned-view').textContent,/inspect · completed.*Synthetic retained child finding/s);
    assert.match(document.querySelector('#owned-view .metrics').textContent,/Input 7Output 3Total —/);
    assert.equal(requests.filter(request=>request.route==='/ui/session').length,1);
    assert.ok(requests.every(request=>request.credentials==='same-origin'&&!request.headers?.Authorization),'Restoration uses the browser session, with no key or master token fixture');
    assert.equal(requests.filter(request=>request.method==='POST'&&request.route!=='/ui/session').length,0,'Loading a retained page cannot submit a run, effect or profile change');
  }finally{window.dispatchEvent(new window.Event('pagehide'));window.close();}
});
