// Synthetic inference; actual compiled native engine, file reads and storage.
import assert from 'node:assert/strict';
import {createServer} from 'node:http';
import {execFile} from 'node:child_process';
import {promisify} from 'node:util';
import {mkdtemp,writeFile,rm} from 'node:fs/promises';
import {tmpdir} from 'node:os';import {join,resolve,dirname,basename} from 'node:path';
const [binary,modules,stdlib]=process.argv.slice(2),root=await mkdtemp(join(tmpdir(),'xmind-graph-children-'));let failure,requests=0;const initial=[];
function reply(response,delta,finish){response.writeHead(200,{'Content-Type':'text/event-stream'});response.end(`data: ${JSON.stringify({choices:[{index:0,delta,finish_reason:finish}]})}\n\ndata: [DONE]\n\n`);}
const peer=createServer((request,response)=>{let raw='';request.on('data',data=>raw+=data);request.on('end',()=>{try{
 ++requests;const body=JSON.parse(raw),user=body.messages.filter(x=>x.role==='user');assert.equal(user.length,1);assert.ok(!raw.includes('root-private-prompt'));const side=user[0].content.slice(6);assert.ok(['left','right'].includes(side));assert.ok(!raw.includes('child-'+(side==='left'?'right':'left')));
 if(body.messages.at(-1).role==='tool'){assert.equal(JSON.parse(body.messages.at(-1).content).content,`Actual ${side} file\n`);reply(response,{content:`Synthetic ${side} final after actual read`},'stop');}
 else {initial.push({side,response});if(initial.length===2){for(const item of initial)reply(item.response,{tool_calls:[{index:0,id:'read-'+item.side,type:'function',function:{name:'read_file',arguments:JSON.stringify({path:item.side+'.txt'})}}]},'tool_calls');}}
 }catch(error){failure=error;response.writeHead(500);response.end('Synthetic graph child peer failed');}});});
try{await writeFile(join(root,'left.txt'),'Actual left file\n');await writeFile(join(root,'right.txt'),'Actual right file\n');await new Promise(resolve=>peer.listen(0,'127.0.0.1',resolve));const result=await promisify(execFile)(binary,[root,modules,stdlib,`http://127.0.0.1:${peer.address().port}/chat`],{windowsHide:true,timeout:30000});if(failure)throw failure;assert.equal(requests,4);assert.equal(initial.length,2);process.stdout.write(result.stdout);}finally{peer.closeAllConnections();await new Promise(resolve=>peer.close(resolve));assert.equal(dirname(resolve(root)),resolve(tmpdir()));assert.ok(basename(root).startsWith('xmind-graph-children-'));await rm(root,{recursive:true,force:true});}
