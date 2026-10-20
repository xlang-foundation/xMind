// Synthetic HTTP event fixtures exercise the real native console renderer.
// These assertions do not claim provider inference or rendered editor coverage.
import assert from 'node:assert/strict';
import {createServer} from 'node:http';
import {execFile} from 'node:child_process';
import {promisify} from 'node:util';

export async function verifyCliUsagePresentation(binary) {
  const execute=promisify(execFile),token='synthetic-cli-usage-auth-'.padEnd(64,'x');
  let fixture;
  const server=createServer((request,response)=>{
    assert.equal(request.method,'GET');assert.equal(request.headers.authorization,'Bearer '+token);
    const url=new URL(request.url,'http://127.0.0.1');let result;
    if(url.pathname==='/v1/runs/root')result={id:'root',session_id:'session',parent_id:'',state:fixture.state,graph_root:!!fixture.children};
    else if(url.pathname==='/v1/health')result={owned_child_observation:false};
    else if(url.pathname==='/v1/graph-runs/root'||url.pathname==='/v1/graph-runs/invocation'){
      const nested=url.pathname.endsWith('/invocation'),leaves={nodes:fixture.children.map(id=>({id,type:'agent',prompt:'Synthetic usage owner'}))},spec=fixture.nested&&!nested?{nodes:[{id:'call',type:'graph',prompt:'Synthetic nested usage',graph_id:'fixture.inner',graph_revision:7,graph_spec:leaves}]}:leaves;
      result={run:{id:nested?'invocation':'root',session_id:'session',parent_id:nested?(fixture.corrupt==='parent'?'foreign':'root'):'',node_id:nested?'call':'',state:fixture.state,graph_root:true},graph_id:nested?'fixture.inner':'fixture.root',graph_revision:nested?(fixture.corrupt==='revision'?8:7):1,checkpoint_revision:1,spec,checkpoint:{nodes:spec.nodes.map(node=>({id:node.id,state:'completed',output:{}}))}};
    }
    else if(url.pathname==='/v1/graph-runs/root/children')result=fixture.nested?[{id:'invocation',session_id:'session',parent_id:'root',node_id:'call',graph_root:true}]:fixture.children.map(id=>({id,session_id:'session',parent_id:'root',node_id:id,graph_root:false}));
    else if(url.pathname==='/v1/graph-runs/invocation/children')result=fixture.children.map(id=>({id,session_id:'session',parent_id:'invocation',node_id:id,graph_root:fixture.corrupt==='role'&&id==='child-a'}));
    else if(url.pathname==='/v1/runs/root/events'||url.pathname==='/v1/graph-runs/root/events')result=fixture.events.filter(e=>e.seq>Number(url.searchParams.get('after')));
    else {response.writeHead(404);response.end('{}');return;}
    response.writeHead(200,{'Content-Type':'application/json'});response.end(JSON.stringify(result));
  });
  await new Promise(resolve=>server.listen(0,'127.0.0.1',resolve));
  const invoke=async(mode='human')=>{
    try{return {...await execute(binary,[String(server.address().port),fixture.children?'graph-watch':'watch','root'],{windowsHide:true,timeout:5000,env:{...process.env,XMIND_AUTH_TOKEN:token,XMIND_CLI_OUTPUT:mode}}),code:0};}
    catch(error){if(typeof error.code==='number')return error;throw error;}
  };
  const set=(records,state='completed',children)=>{fixture={events:records.map(([kind,data={},run_id='root'],index)=>({seq:index+1,run_id,kind,data})),state,children};};
  const lines=result=>result.stderr.split(/\r?\n/).filter(line=>line.endsWith(' tokens'));
  try {
    const final={input_tokens:80,output_tokens:9,total_tokens:100,output_tokens_details:{reasoning_tokens:11},input_tokens_details:{cached_tokens:20}};
    set([
      ['model.text',{text:'Answer begins '}],['model.usage',{input_tokens:70,output_tokens:1}],
      ['model.usage',final],['model.text',{text:'and ends.'}],['model.usage',final],['model.done'],
      ['budget.model_call.finished',{was_started:true}],
      // Equal usage on a distinct response is still printed for that response.
      ['model.text',{text:'Second answer.'}],['model.usage',final],['model.done'],['run.completed']
    ]);
    let result=await invoke();assert.equal(result.code,0,result.stderr);
    assert.equal(result.stdout,'Answer begins and ends.Second answer.');
    assert.deepEqual(lines(result),Array(2).fill('Input 80 · Output 9 · Reasoning 11 · Total 100 · Cached 20 tokens'));
    const raw=await invoke('json');assert.equal(raw.code,0,raw.stderr);assert.deepEqual(raw.stdout.trim().split(/\r?\n/).map(JSON.parse),fixture.events,'NDJSON preserves every supplied event, including duplicates and intermediate usage');assert.equal(raw.stderr,'');

    // Two interleaved graph owners must never overwrite each other's usage.
    set([
      ['model.usage',{input_tokens:10,output_tokens:2},'child-a'],
      ['model.usage',{input_tokens:30,output_tokens:4},'child-b'],
      ['model.usage',{input_tokens:12,output_tokens:3},'child-a'],
      ['model.done',{},'child-b'],['model.done',{},'child-a'],['run.completed']
    ],'completed',['child-a','child-b']);
    result=await invoke();assert.equal(result.code,0,result.stderr);assert.deepEqual(lines(result),['Input 30 · Output 4 tokens','Input 12 · Output 3 tokens']);

    fixture.nested=true;result=await invoke();assert.equal(result.code,0,result.stderr);assert.deepEqual(lines(result),['Input 30 · Output 4 tokens','Input 12 · Output 3 tokens'],'Nested leaf usage must remain scoped to its actual admitted owner');
    const nestedRaw=await invoke('json');assert.equal(nestedRaw.code,0,nestedRaw.stderr);assert.deepEqual(nestedRaw.stdout.trim().split(/\r?\n/).map(JSON.parse),fixture.events,'Recursive graph observation preserves each descendant event');

    for(const corrupt of ['revision','parent','role']){fixture.corrupt=corrupt;const rejected=await invoke('json');assert.equal(rejected.code,1,rejected.stderr);assert.equal(rejected.stdout,'','Invalid nested ownership must reject the batch before emitting events');assert.match(rejected.stderr,/Graph invocation immutable snapshot changed|Invalid graph child role/);}delete fixture.corrupt;

    set([['model.usage',{input_tokens:5}],['budget.model_call.finished',{was_started:true}],['run.failed',{reason:'Fixture provider stopped'}]],'failed');
    result=await invoke();assert.equal(result.code,1);assert.deepEqual(lines(result),['Input 5 tokens']);assert.match(result.stderr,/Fixture provider stopped/);
    set([['model.usage',{output_tokens:3}],['run.failed',{reason:'Fixture provider stopped before model.done'}]],'failed');
    result=await invoke();assert.equal(result.code,1);assert.deepEqual(lines(result),['Output 3 tokens']);
    set([['model.usage',{total_tokens:7}]],'cancelled');
    result=await invoke();assert.equal(result.code,2);assert.deepEqual(lines(result),['Total 7 tokens']);assert.match(result.stderr,/Run cancelled/);

    set([
      ['model.usage',{input_tokens:0,output_tokens:0,input_tokens_scope:'uncached',cache_read_input_tokens:0,cache_creation_input_tokens:4}],['model.done'],
      ['model.usage',{prompt_tokens:9,completion_tokens:2,completion_tokens_details:{reasoning_tokens:1}}],['model.done'],
      ['model.usage',{output_tokens:1,thoughtsTokenCount:3}],['model.done'],
      ['model.usage',{input_tokens:-1,output_tokens:'9',total_tokens:1.5,cached_tokens:'4'}],['model.done'],
      ['model.usage',{input_tokens:2**63}],['model.done'],
      ['model.usage',{}],['model.done'],['model.done'],['run.completed']
    ]);
    result=await invoke();assert.equal(result.code,0,result.stderr);assert.deepEqual(lines(result),['Input (uncached) 0 · Output 0 · Cached 0 · Cache write 4 tokens','Input 9 · Output 2 · Reasoning 1 tokens','Output 1 · Reasoning 3 tokens']);assert.doesNotMatch(result.stderr,/Total /,'Missing totals are not computed');
    console.log('Native console usage presentation passed: synthetic snapshots, response/owner boundaries, failure/cancellation, missing/invalid counts, unchanged NDJSON.');
  }finally{server.closeAllConnections();await new Promise(resolve=>server.close(resolve));}
}
