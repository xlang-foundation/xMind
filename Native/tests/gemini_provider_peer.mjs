// Independent synthetic socket peer; no product provider or live model.
import assert from 'node:assert/strict';
import {createServer} from 'node:http';
import {execFile} from 'node:child_process';
import {promisify} from 'node:util';
const execute=promisify(execFile),routes=[];let failure;
const suffix='/v1beta/models/fixture-gemini:streamGenerateContent?alt=sse';
const frame=value=>`data: ${JSON.stringify(value)}\n\n`;
const server=createServer((request,response)=>{routes.push(request.url);let source='';request.on('data',data=>{source+=data;if(source.length>1024*1024)request.destroy();});request.on('end',()=>{try{
 assert.equal(request.method,'POST');assert.equal(request.headers['x-goog-api-key'],'gemini-wire-fixture-not-a-real-key');assert.equal(request.headers.authorization,undefined);assert.equal(request.headers['x-api-key'],undefined);assert.equal(request.headers['anthropic-version'],undefined);assert.equal(request.headers['content-type'],'application/json');assert.equal(request.headers.accept,'text/event-stream');assert.ok(!source.includes('gemini-wire-fixture-not-a-real-key'));
 assert.ok(request.url.endsWith(suffix));const route=request.url.slice(0,-suffix.length);const body=JSON.parse(source);assert.deepEqual(Object.keys(body).sort(),['contents','generationConfig','systemInstruction','tools']);assert.deepEqual(body.systemInstruction,{parts:[{text:'Native fixture instructions'}]});assert.deepEqual(body.generationConfig,{maxOutputTokens:64});assert.deepEqual(body.tools,[{functionDeclarations:[{name:'read_file',description:'Read a file',parametersJsonSchema:{type:'object',properties:{path:{type:'string'}},required:['path']}}]}]);
 const contents=[{role:'user',parts:[{text:'Read fixture'}]}];if(route==='/continuation'){contents.push({role:'model',parts:[{functionCall:{name:'read_file',args:{path:'README',n:1.2345678901234567890123456789},id:'provider-call'},thoughtSignature:'c3ludGhldGlj'}]},{role:'user',parts:[{functionResponse:{name:'read_file',response:{content:'actual fixture result'},id:'provider-call'}}]});assert.ok(source.includes('1.2345678901234567890123456789'));}assert.deepEqual(body.contents,contents);
 if(route==='/unauthorized'){response.writeHead(401,{'Content-Type':'application/json'});response.end(JSON.stringify({error:{message:'private fixture detail'}}));return;}if(route==='/redirect'){response.writeHead(307,{Location:'/must-not-arrive'+suffix});response.end();return;}
 response.writeHead(200,{'Content-Type':'text/event-stream'});
 if(route==='/blocked'){response.end(frame({promptFeedback:{blockReason:'SAFETY'}}));return;}
 const text=route==='/continuation';const part=text?'"text":"Fixture complete"':`"functionCall":{"name":"${route==='/unknown'?'unoffered_tool':'read_file'}","id":"provider-call","args":{"path":"README","n":1.2345678901234567890123456789}},"thoughtSignature":"c3ludGhldGlj"`;
 const wire=`data: {"candidates":[{"index":0,"content":{"role":"model","parts":[{${part}}]}${route==='/incomplete'?'':',"finishReason":"STOP"'}}],"modelVersion":"fixture-gemini","usageMetadata":{"promptTokenCount":8,"candidatesTokenCount":5}}\n\n`;
 // Arbitrary byte fragments test the actual transport/decoder boundary.
 const bytes=Buffer.from(wire);for(let offset=0;offset<bytes.length;offset+=7)response.write(bytes.subarray(offset,offset+7));if(route==='/late-error')response.write(frame({error:{message:'private fixture detail'}}));response.end();
 }catch(error){failure=error;if(!response.headersSent)response.writeHead(400);response.end();}});});
try{await new Promise(resolve=>server.listen(0,'127.0.0.1',resolve));const result=await execute(process.argv[2],[`http://127.0.0.1:${server.address().port}`],{windowsHide:true,timeout:20000});if(failure)throw failure;assert.deepEqual(routes,['', '/continuation','/unknown','/incomplete','/late-error','/blocked','/unauthorized','/redirect'].map(route=>route+suffix));process.stdout.write(result.stdout);}finally{server.closeAllConnections();await new Promise(resolve=>server.close(resolve));}
