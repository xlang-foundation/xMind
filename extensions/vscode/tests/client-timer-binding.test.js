'use strict';
const test=require('node:test');
const assert=require('node:assert/strict');
const fs=require('node:fs');
const vm=require('node:vm');
const path=require('node:path');

test('event subscriptions call browser timer APIs with the global receiver',async()=>{
 const timers=[],cleared=[];
 const sandbox={URL,AbortController,AbortSignal,TextDecoder,TextEncoder,fetch:globalThis.fetch};
 const context=vm.createContext(sandbox),realm=vm.runInContext('globalThis',context);
 sandbox.setTimeout=function(callback,delay){assert.equal(this,realm);const handle={callback,delay};timers.push(handle);return handle;};
 sandbox.clearTimeout=function(handle){assert.equal(this,realm);cleared.push(handle);};
 vm.runInContext(fs.readFileSync(path.resolve(__dirname,'../client.js'),'utf8'),context,{filename:'client.js'});
 const {EventStreamSubscription}=realm.XMindBackend,errors=[];
 const client={baseUrl:'http://127.0.0.1:8765',eventStream:async()=>({cursor:0,reason:'reconnect'})};
 const subscription=new EventStreamSubscription({current:()=>true,onEvent:()=>{},onError:error=>errors.push(error)});
 assert.doesNotThrow(()=>subscription.watch({client,root:'root',session:'session',scope:'run',generation:1,after:0}));
 await new Promise(resolve=>setImmediate(resolve));
 assert.equal(errors.length,0);
 assert.equal(timers.length,1);
 subscription.dispose();
 assert.deepEqual(cleared.filter(Boolean),timers);
});
