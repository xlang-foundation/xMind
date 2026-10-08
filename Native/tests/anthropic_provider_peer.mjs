// Independent synthetic HTTP peer; never a product provider or live inference.
import assert from 'node:assert/strict';
import {createServer} from 'node:http';
import {execFile} from 'node:child_process';
import {promisify} from 'node:util';
const execute=promisify(execFile),routes=[];let failure;
const event=value=>`event: ${value.type}\ndata: ${JSON.stringify(value)}\n\n`;
const server=createServer((request,response)=>{
  routes.push(request.url);let source='';request.on('data',data=>{source+=data;});
  request.on('end',()=>{try{
    assert.equal(request.method,'POST');assert.equal(request.headers['x-api-key'],'claude-wire-fixture-not-a-real-key');
    assert.equal(request.headers.authorization,undefined);assert.equal(request.headers['x-goog-api-key'],undefined);
    assert.equal(request.headers['anthropic-version'],'2023-06-01');assert.equal(request.headers['content-type'],'application/json');
    assert.equal(request.headers.accept,'text/event-stream');assert.ok(!source.includes('claude-wire-fixture-not-a-real-key'));
    const body=JSON.parse(source);assert.deepEqual(Object.keys(body).sort(),['max_tokens','messages','model','stream','system','tools']);
    assert.equal(body.model,'fixture-claude');assert.equal(body.stream,true);assert.equal(body.max_tokens,64);
    assert.deepEqual(body.system,[{type:'text',text:'Native fixture instructions'}]);
    assert.deepEqual(body.tools,[{name:'read_file',description:'Read a file',input_schema:{type:'object',properties:{path:{type:'string'}},required:['path']}}]);
    const messages=[{role:'user',content:[{type:'text',text:'Read fixture'}]}];
    if(request.url==='/continuation')messages.push({role:'assistant',content:[{type:'tool_use',id:'toolu-fixture',name:'read_file',input:{path:'README'}}]},{role:'user',content:[{type:'tool_result',tool_use_id:'toolu-fixture',content:'actual fixture result'}]});
    assert.deepEqual(body.messages,messages);
    if(request.url==='/unauthorized'){response.writeHead(401,{'Content-Type':'application/json'});response.end(JSON.stringify({error:{type:'authentication_error',message:'private fixture detail'}}));return;}
    if(request.url==='/redirect'){response.writeHead(307,{Location:'/must-not-arrive'});response.end();return;}
    response.writeHead(200,{'Content-Type':'text/event-stream'});
    const zero=request.url==='/zero-cache',usage={input_tokens:zero?0:8,output_tokens:0};
    if(request.url==='/messages')Object.assign(usage,{cache_creation_input_tokens:3,cache_read_input_tokens:7});
    if(zero)Object.assign(usage,{cache_creation_input_tokens:0,cache_read_input_tokens:0});
    response.write(event({type:'message_start',message:{id:'msg-fixture',type:'message',role:'assistant',model:'fixture-claude',content:[],stop_reason:null,stop_sequence:null,usage}}));
    const text=request.url==='/continuation'||zero;
    response.write(event({type:'content_block_start',index:0,content_block:text?{type:'text',text:''}:{type:'tool_use',id:'toolu-fixture',name:request.url==='/unknown'?'unoffered_tool':'read_file',input:{}}}));
    response.write(event({type:'content_block_delta',index:0,delta:text?{type:'text_delta',text:'Fixture complete'}:{type:'input_json_delta',partial_json:'{"path":"README"}'}}));
    if(request.url==='/incomplete'){response.end();return;}
    response.write(event({type:'content_block_stop',index:0}));
    response.write(event({type:'message_delta',delta:{stop_reason:text?'end_turn':'tool_use',stop_sequence:null},usage:{output_tokens:request.url==='/bad-usage'?'5':zero?0:5}}));
    response.write(event({type:'message_stop'}));
    if(request.url==='/late-error')response.write(event({type:'error',error:{type:'overloaded_error',message:'private fixture detail'}}));
    response.end();
  }catch(error){failure=error;if(!response.headersSent)response.writeHead(400);response.end();}});
});
try{
  await new Promise(resolve=>server.listen(0,'127.0.0.1',resolve));
  const result=await execute(process.argv[2],[`http://127.0.0.1:${server.address().port}`],{windowsHide:true,timeout:20000});
  if(failure)throw failure;
  assert.deepEqual(routes,['/messages','/continuation','/zero-cache','/unknown','/incomplete','/late-error','/bad-usage','/unauthorized','/redirect']);
  process.stdout.write(result.stdout);
}finally{server.closeAllConnections();await new Promise(resolve=>server.close(resolve));}
