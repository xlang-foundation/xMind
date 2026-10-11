import assert from 'node:assert/strict';
import {spawn} from 'node:child_process';
import {createRequire} from 'node:module';
import {dirname,join} from 'node:path';
import {fileURLToPath} from 'node:url';

const here=dirname(fileURLToPath(import.meta.url));
const require=createRequire(join(here,'sdk','package.json'));
const express=require('express');
const {DefaultRequestHandler,InMemoryTaskStore,AgentEvent}=require('@a2a-js/sdk/server');
const {Role,TaskState}=require('@a2a-js/sdk');
const {agentCardHandler,jsonRpcHandler,UserBuilder}=require('@a2a-js/sdk/server/express');
const {ClientFactory,DefaultAgentCardResolver,JsonRpcTransportFactory}=require('@a2a-js/sdk/client');
const executable=process.argv[2],token='xmind-a2a-sdk-contract-bearer';
assert.ok(executable,'Expected the native outbound client contract executable');
let appServer;
const application=express();
application.use((request,response,next)=>{
  if(request.headers.authorization!=='Bearer '+token){response.status(401).json({detail:'Bearer authorization required'});return;}
  next();
});
const card={
  name:'Pinned official A2A SDK peer',description:'Independent SDK contract peer for xMind native outbound A2A',version:'1.3.0',
  supportedInterfaces:[],provider:undefined,
  capabilities:{streaming:false,pushNotifications:false,stateTransitionHistory:true},
  securitySchemes:{xmindPeer:{httpAuthSecurityScheme:{scheme:'Bearer',bearerFormat:'opaque'}}},
  securityRequirements:[{schemes:{xmindPeer:{}}}],defaultInputModes:['text/plain'],defaultOutputModes:['text/plain'],skills:[],signatures:[]
};
const taskContexts=new Map();
const executor={
  async execute(context,eventBus){
    taskContexts.set(context.taskId,context.contextId);
    eventBus.publish(AgentEvent.task({id:context.taskId,contextId:context.contextId,status:{state:TaskState.TASK_STATE_SUBMITTED,timestamp:new Date().toISOString()},artifacts:[],history:[context.userMessage],metadata:{}}));
    eventBus.publish(AgentEvent.statusUpdate({taskId:context.taskId,contextId:context.contextId,status:{state:TaskState.TASK_STATE_INPUT_REQUIRED,timestamp:new Date().toISOString()},metadata:{}}));
  },
  async cancelTask(taskId,eventBus){
    const contextId=taskContexts.get(taskId);
    eventBus.publish(AgentEvent.statusUpdate({taskId,contextId,status:{state:TaskState.TASK_STATE_CANCELED,timestamp:new Date().toISOString()},metadata:{}}));
  }
};
const handler=new DefaultRequestHandler(card,new InMemoryTaskStore(),executor);
application.use('/.well-known/agent-card.json',(request,response,next)=>{card.supportedInterfaces=[{url:`http://127.0.0.1:${appServer.address().port}/a2a`,protocolBinding:'JSONRPC',protocolVersion:'1.0'}];next();},agentCardHandler({agentCardProvider:handler}));
application.use('/a2a',jsonRpcHandler({requestHandler:handler,userBuilder:UserBuilder.noAuthentication}));
try{
  await new Promise((resolve,reject)=>{appServer=application.listen(0,'127.0.0.1',resolve);appServer.once('error',reject);});
  const url=`http://127.0.0.1:${appServer.address().port}/.well-known/agent-card.json`;
  if(executable==='--sdk-self-check'){
    const authenticatedFetch=(destination,options={})=>{const headers=new Headers(options.headers);headers.set('Authorization','Bearer '+token);return fetch(destination,{...options,headers,redirect:'error'});};
    const deadline=async(label,operation)=>{process.stdout.write('SDK peer self-check: '+label+'\n');return Promise.race([operation, new Promise((_,reject)=>setTimeout(()=>reject(new Error('SDK peer self-check timed out at '+label)),5000))]);};
    const peer=await new ClientFactory({cardResolver:new DefaultAgentCardResolver({fetchImpl:authenticatedFetch}),transports:[new JsonRpcTransportFactory({fetchImpl:authenticatedFetch})]}).createFromUrl(`http://127.0.0.1:${appServer.address().port}`);
    process.stdout.write('SDK peer self-check: discovered\n');
    const result=await deadline('SendMessage',peer.sendMessage({message:{messageId:'sdk-self-check',role:Role.ROLE_USER,parts:[{content:{$case:'text',value:'test the pinned peer'}}]}}));
    assert.ok(result.id);const fetched=await deadline('GetTask',peer.getTask({id:result.id}));assert.equal(fetched.id,result.id);const cancelled=await deadline('CancelTask',peer.cancelTask({id:result.id}));assert.equal(cancelled.id,result.id);
    process.stdout.write('Pinned SDK peer self-check passed card discovery and authenticated SendMessage/GetTask/CancelTask.\n');
  }else{
    const child=spawn(executable,[url,token],{stdio:['ignore','pipe','pipe'],windowsHide:true});let stdout='',stderr='';
    child.stdout.setEncoding('utf8').on('data',chunk=>stdout+=chunk);child.stderr.setEncoding('utf8').on('data',chunk=>stderr+=chunk);
    const result=await new Promise((resolve,reject)=>{
      const timer=setTimeout(()=>{child.kill();reject(new Error('Native outbound SDK peer contract exceeded its deadline'));},25000);
      child.once('error',error=>{clearTimeout(timer);reject(error);});
      child.once('exit',(code,signal)=>{clearTimeout(timer);resolve({code,signal});});
    });
    assert.equal(result.code,0,`Native SDK-peer client failed (${result.signal||result.code}): ${stderr}`);
    assert.match(stdout,/independent official A2A SDK 1\.3\.0 AgentCard discovery, SendMessage, GetTask and CancelTask/);
    process.stdout.write(stdout);
  }
}finally{
  if(appServer)await new Promise(resolve=>appServer.close(resolve));
}
