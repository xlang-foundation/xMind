'use strict';
const {test}=require('node:test');
const assert=require('node:assert/strict');
const {BrowserSessionClient}=require('../browser.js');
const {EventStreamSubscription}=require('../../../extensions/vscode/client');

const frame=(kind,data,id)=>`${id===undefined?'':`id: ${id}\n`}event: ${kind}\ndata: ${JSON.stringify(data)}\n\n`;
const run={id:'root',session_id:'session',state:'running',parent_id:'',node_id:'',graph_root:false};
const event=seq=>({seq,run_id:'root',kind:'model.text',data:{text:`Committed ${seq}`}});
async function until(predicate){
  for(let attempt=0;attempt<30;attempt++){
    if(predicate())return;
    await new Promise(resolve=>setImmediate(resolve));
  }
  throw Error('Stream observation did not settle');
}

test('browser stream deadline reconnects from the accepted cursor without replaying effects',async t=>{
  t.mock.timers.enable({apis:['setTimeout']});
  const requests=[],delivered=[],errors=[],tasks=[],ended=[],encoder=new TextEncoder();
  const client=new BrowserSessionClient('http://127.0.0.1:60405',{
    fetchImpl:async(url,options)=>{
      requests.push({url,options});
      const after=Number(new URL(url).searchParams.get('after'));
      const observation=frame('observation',{run,after,scope:'run'});
      if(requests.length===1){
        return new Response(new ReadableStream({start(controller){
          controller.enqueue(encoder.encode(observation+frame('committed',event(1),1)));
          options.signal.addEventListener('abort',()=>controller.error(options.signal.reason),{once:true});
        }}),{headers:{'Content-Type':'text/event-stream'}});
      }
      return new Response(observation+frame('committed',event(2),2)+frame('end',{reason:'terminal',after:2}),{
        headers:{'Content-Type':'text/event-stream'}
      });
    }
  });
  const subscription=new EventStreamSubscription({
    current:()=>true,onEvent:value=>delivered.push(value.seq),onError:error=>errors.push(error),
    onEnd:value=>ended.push(value),schedule:(callback,delay)=>{tasks.push({callback,delay});return callback;},unschedule:()=>{}
  });
  try{
    subscription.watch({client,root:'root',session:'session',scope:'run',generation:0,after:0});
    await until(()=>subscription.cursor===1);
    const parentSignal=subscription.abort.signal;
    t.mock.timers.tick(35000);
    await until(()=>tasks.length||errors.length);
    assert.deepEqual(errors,[],'A request deadline must use bounded read-only transport recovery');
    assert.equal(parentSignal.aborted,false,'Deadline expiry must not cancel the selected subscription');
    assert.equal(tasks.length,1);
    assert.equal(tasks[0].delay,500,'Keep the existing retry schedule');
    tasks.shift().callback();
    await until(()=>ended.length===1);
    assert.deepEqual(delivered,[1,2]);
    assert.equal(new URL(requests[1].url).searchParams.get('after'),'1');
    assert.equal(requests.length,2);
    assert.ok(requests.every(request=>request.options.method==='GET'));
    assert.equal(ended[0].reason,'terminal');
    assert.equal(subscription.cursor,2);
  }finally{
    subscription.dispose();
    t.mock.timers.reset();
  }
});
