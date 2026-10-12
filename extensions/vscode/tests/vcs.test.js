'use strict';
const test=require('node:test');
const assert=require('node:assert/strict');
const {BackendClient,validateVcsBase,validateVcsDiff}=require('../client');

test('Git review client encodes the selected mode, base and context at its authenticated backend',async()=>{
 const calls=[],token='synthetic-vcs-token-32-bytes-long',base={name:'feature/review',ref:'a'.repeat(40),source:'reflog'},diff=[{file:'src/file with spaces.js',patch:'diff --git a/file b/file\n',additions:2,deletions:1,status:'modified'}];
 const client=new BackendClient('http://localhost:8765',()=>token,async(url,options)=>{calls.push({url,options});return {ok:true,json:async()=>new URL(url).pathname.endsWith('/vcs/base')?base:diff};});
 assert.deepEqual(await client.vcsBase('workspace-folder-2'),base);assert.deepEqual(await client.vcsDiff('committed','refs/heads/review/base',17,'workspace-folder-2'),diff);
 assert.equal(calls[0].url,'http://127.0.0.1:8765/v1/vcs/base?folder_id=workspace-folder-2');assert.equal(calls[1].url,'http://127.0.0.1:8765/v1/vcs/diff?mode=committed&context=17&base=refs%2Fheads%2Freview%2Fbase&folder_id=workspace-folder-2');
 assert.equal(calls[1].options.headers.Authorization,'Bearer '+token);assert.equal(calls[1].options.method,'GET');assert.equal(calls[1].options.body,undefined);
});

test('Git review client rejects malformed native bases, patches and user selections',async()=>{
 assert.equal(validateVcsBase(null),null);assert.throws(()=>validateVcsBase({name:'main',ref:'bad',source:'default'}),/review base/);
 assert.throws(()=>validateVcsDiff([{file:'x',patch:'',additions:0,deletions:0,status:'renamed'}]),/diff entry/);
 const client=new BackendClient('http://127.0.0.1:8765',()=> 'synthetic-vcs-token-32-bytes-long',async()=>({ok:true,json:async()=>[]}));
 await assert.rejects(client.vcsDiff('other'),/selection/);await assert.rejects(client.vcsDiff('branch','bad\nref'),/base reference/);await assert.rejects(client.vcsDiff('working',undefined,1001),/selection/);
});
