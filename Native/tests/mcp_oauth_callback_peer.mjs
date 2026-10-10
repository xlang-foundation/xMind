import assert from 'node:assert/strict';
import {spawn} from 'node:child_process';
import {request} from 'node:http';
import {once} from 'node:events';

const executable=process.argv[2];
const call=(url,options={})=>new Promise((resolve,reject)=>{
  const outgoing=request(url,{method:'GET',...options},response=>{
    let body='';response.setEncoding('utf8');response.on('data',chunk=>{body+=chunk;assert.ok(body.length<8192);});response.on('end',()=>resolve({status:response.statusCode,headers:response.headers,body}));
  });outgoing.on('error',reject);outgoing.setTimeout(2000,()=>outgoing.destroy(new Error('Callback fixture request timeout')));outgoing.end();
});
for(const mode of ['valid','bad','duplicate','missing-issuer','encoded-target','denial','cancel','pre-cancel','timeout','owner-expiry','binding','deadline']){
  const child=spawn(executable,[mode],{windowsHide:true,stdio:['pipe','pipe','pipe']});
  let stdout='',stderr='',settled=false;let lineResolve,lineReject,retiredResolve,retiredReject;
  const firstLine=new Promise((resolve,reject)=>{lineResolve=resolve;lineReject=reject;});
  const retired=new Promise((resolve,reject)=>{retiredResolve=resolve;retiredReject=reject;});
  child.stdout.setEncoding('utf8');child.stderr.setEncoding('utf8');
  child.stdout.on('data',chunk=>{stdout+=chunk;if(!settled&&stdout.includes('\n')){settled=true;try{lineResolve(JSON.parse(stdout.slice(0,stdout.indexOf('\n'))));}catch(error){lineReject(error);}}if(stdout.includes('Native callback fixture passed'))retiredResolve();});
  child.stderr.on('data',chunk=>{stderr+=chunk;});child.on('error',lineReject);
  const exited=once(child,'exit');child.on('exit',()=>{if(!settled)lineReject(new Error(stderr||'Callback fixture exited before metadata'));if(!stdout.includes('Native callback fixture passed'))retiredReject(new Error(stderr||'Callback fixture exited before retirement'));});
  const timer=setTimeout(()=>child.kill(),10000);
  try {
    const metadata=await firstLine;
    const redirect=new URL(metadata.redirect),authorize=new URL(metadata.authorization_url);
    assert.equal(redirect.hostname,'127.0.0.1');assert.equal(redirect.pathname,'/oauth/callback/native-test');
    const state=authorize.searchParams.get('state');assert.equal(state.length,43);
    if(['valid','bad','duplicate','missing-issuer','encoded-target','denial'].includes(mode)){
      for(const [url,options,expected] of [[redirect,{headers:{Host:'attacker.example.test'}},400],[redirect,{headers:{Origin:'https://attacker.example.test'}},400],[redirect,{headers:{Authorization:'Bearer synthetic-unrelated-credential'}},400],[new URL('/wrong-path',redirect),{},404],[redirect,{method:'POST'},404]]){
        const rejected=await call(url,options);assert.equal(rejected.status,expected);assert.ok(!rejected.body.includes(state));
      }
      const callback=new URL(redirect);callback.searchParams.set('state',mode==='bad'?'synthetic-wrong-state':state);callback.searchParams.set('iss','https://issuer.example.test');
      if(mode==='duplicate')callback.searchParams.append('state',state);
      if(mode==='missing-issuer')callback.searchParams.delete('iss');
      if(mode==='encoded-target')callback.pathname=callback.pathname.replace('native-test','%6eative-test');
      if(mode==='denial'){callback.searchParams.set('error','access_denied');callback.searchParams.set('error_description','synthetic-private-description-never-reflect');}
      else callback.searchParams.set('code','synthetic-private-code+&=');
      const response=await call(callback);
      assert.equal(response.status,mode==='valid'?200:400);
      assert.equal(response.headers['cache-control'],'no-store');assert.equal(response.headers['referrer-policy'],'no-referrer');assert.equal(response.headers['x-content-type-options'],'nosniff');
      assert.ok(response.headers['content-security-policy'].includes("default-src 'none'"));
      for(const value of [state,'synthetic-private-code','synthetic-private-description'])assert.ok(!response.body.includes(value),'Callback response must not reflect private query fields');
      assert.ok(!response.headers.location,'Callback must not redirect to an untrusted target');
    }
    await retired;assert.equal(child.exitCode,null,'Native fixture must remain alive during the socket check');
    await assert.rejects(call(redirect),undefined,'Single-use receiver must release its socket while its native process and receiver are still alive');
    assert.equal(child.exitCode,null,'Process exit must not substitute for native listener retirement');
    child.stdin.end('\n');
    const [code,signal]=await exited;assert.equal(signal,null);assert.equal(code,0,stderr);assert.match(stdout,/Native callback fixture passed/);
    process.stdout.write(`Actual native loopback callback passed ${mode}; exclusive binding and socket retirement checked while the native process stayed alive.\n`);
  }finally {clearTimeout(timer);if(child.exitCode===null&&child.signalCode===null){child.kill();await exited;}}
}
process.stdout.write('Synthetic browser redirects exercised the production native listener/authorization owner. No trusted HTTPS login, token exchange, persistence or product UI integration verified.\n');
