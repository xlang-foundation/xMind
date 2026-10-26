'use strict';
const {test}=require('node:test');
const assert=require('node:assert/strict');
const {BackendAuth}=require('../backend-auth');
const origin='http://127.0.0.1:8765',first='a'.repeat(64),second='b'.repeat(64);
function fixture(native=new Map(),options={}){
  const secrets=new Map(),listeners=new Set(),calls=[];
  const context={secrets:{
    async get(key){return secrets.get(key);},
    async store(key,value){secrets.set(key,value);for(const f of listeners)f({key});},
    async delete(key){secrets.delete(key);for(const f of listeners)f({key});},
    onDidChange(f){listeners.add(f);return{dispose(){listeners.delete(f);}};}
  }};
  const vscode={UIKind:{Desktop:1,Web:2},env:{uiKind:options.desktop?1:2},workspace:{isTrusted:true,
    workspaceFolders:[{uri:{scheme:'file',fsPath:'D:\\project'}}],getConfiguration(){return{inspect(){return{};}};}}};
  const deps={platform:'win32',arch:'x64',env:{SystemRoot:'C:\\Windows',OPENAI_API_KEY:'must-not-inherit',XMIND_AUTH_TOKEN:'must-not-inherit'},
    async resolveRuntime(){return{qualified:true,nativeProgram:'D:\\runtime\\xmind.exe',runtimeRoot:'D:\\runtime',privateStateRoot:options.privateRoot||'D:\\private'};},
    async execute(program,args,settings,input){
      calls.push({program,args,settings,input});
      if(args[0]==='--help')return options.oldRuntime?'old help':'xmind client-auth get|put|seed|delete DIRECTORY ORIGIN';
      if(options.failure)throw new Error(first);
      const action=args[1],key=args[3];
      if(action==='get')return JSON.stringify({token:native.get(key)||null});
      if(action==='delete'){native.delete(key);return '{"deleted":true}';}
      const token=JSON.parse(input).token;
      if(options.seedRace&&action==='seed')native.set(key,second);
      if(action==='put'||!native.has(key))native.set(key,token);
      return '{"saved":true}';
    }};
  const auth=new BackendAuth(vscode,context,deps);
  return{auth,vscode,context,secrets,calls,native};
}
test('web host reconnect restores a token into fresh ephemeral SecretStorage',async()=>{
  const one=fixture();await one.auth.store(origin,first);one.auth.dispose();
  const two=fixture(one.native);assert.equal(two.secrets.size,0);
  assert.equal(await two.auth.get('http://localhost:8765/'),first);
  assert.equal(two.secrets.get('xmind.auth:'+origin),first);
  const count=two.calls.length;await Promise.resolve();assert.equal(await two.auth.get(origin),first);
  assert.equal(two.calls.length,count,'own SecretStorage notifications must not cause repeated native reads');
  for(const call of one.calls.concat(two.calls)){
    assert.ok(!call.args.some(value=>value.includes(first)),'tokens must stay out of argv');
    assert.equal(call.settings.env.OPENAI_API_KEY,undefined);assert.equal(call.settings.env.XMIND_AUTH_TOKEN,undefined);
  }
});
test('desktop continues using its existing SecretStorage without a native helper',async()=>{
  const f=fixture(new Map(),{desktop:true});await f.auth.store(origin,first);
  assert.equal(await f.auth.get(origin),first);assert.equal(f.calls.length,0);
});
test('saved credentials remain scoped to the canonical origin',async()=>{
  const f=fixture();await f.auth.store(origin,first);
  assert.equal(await f.auth.get('http://127.0.0.1:8766'),undefined);
  await assert.rejects(f.auth.get('http://example.com:8765'),/loopback/);
});
test('a native rotation wins over stale ephemeral credentials',async()=>{
  const f=fixture(new Map([[origin,second]]));f.secrets.set('xmind.auth:'+origin,first);
  assert.equal(await f.auth.get(origin),second);assert.equal(f.secrets.get('xmind.auth:'+origin),second);
});
test('seeding old SecretStorage never overwrites a concurrent native rotation',async()=>{
  const f=fixture(new Map(),{seedRace:true});f.secrets.set('xmind.auth:'+origin,first);
  assert.equal(await f.auth.get(origin),second);assert.equal(f.native.get(origin),second);
});
test('native failure does not silently fall back to an ephemeral save or expose IPC secrets',async()=>{
  const f=fixture(new Map(),{failure:true});
  await assert.rejects(f.auth.store(origin,first),error=>!error.message.includes(first)&&/preserved/.test(error.message));
  assert.equal(f.secrets.size,0);assert.equal(f.native.size,0);
});
test('old runtimes are detected with safe help before any unknown console command',async()=>{
  const f=fixture(new Map(),{oldRuntime:true});await assert.rejects(f.auth.store(origin,first),/matching/);
  assert.deepEqual(f.calls.map(c=>c.args),[['--help']]);assert.equal(f.secrets.size,0);
});
test('untrusted workspaces and storage overlap fail before starting a helper',async()=>{
  const f=fixture();f.vscode.workspace.isTrusted=false;
  await assert.rejects(f.auth.get(origin),/Trust/);await assert.rejects(f.auth.store(origin,first),/Trust/);assert.equal(f.calls.length,0);
  const overlap=fixture(new Map(),{privateRoot:'D:\\project\\private'});
  await assert.rejects(overlap.auth.store(origin,first),/overlaps/);assert.equal(overlap.calls.length,0);
});
test('explicit rotation and deletion persist across host instances',async()=>{
  const one=fixture();await one.auth.store(origin,first);await one.auth.store(origin,second);
  const two=fixture(one.native);assert.equal(await two.auth.get(origin),second);await two.auth.delete(origin);
  const three=fixture(one.native);assert.equal(await three.auth.get(origin),undefined);
});
