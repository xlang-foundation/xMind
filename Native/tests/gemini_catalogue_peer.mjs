// Independent synthetic Gemini models.list peer. The child uses production
// native catalogue parsing and the actual header-authenticated HTTP transport.
import assert from 'node:assert/strict';
import {createServer} from 'node:http';
import {execFile} from 'node:child_process';
import {promisify} from 'node:util';
const executable=process.argv[2];assert.ok(executable,'Pass the native Gemini catalogue contract executable');const execute=promisify(execFile);
const key='fixture-gemini-catalogue-key',opaque='opaque /+=?&% "\\ 雪',counts=new Map(),held=[];let failure;
const model=(name,methods=['generateContent'])=>({name,supportedGenerationMethods:methods});
const peer=createServer((request,response)=>{let source='';request.on('data',chunk=>source+=chunk);request.on('end',()=>{try{
 assert.equal(request.method,'GET');assert.equal(source,'');assert.equal(request.headers['x-goog-api-key'],key);assert.equal(request.headers.authorization,undefined);assert.equal(request.headers['x-api-key'],undefined);assert.equal(request.headers['anthropic-version'],undefined);assert.equal(request.headers.accept,'application/json');assert.ok(!request.url.includes(key));
 const url=new URL(request.url,'http://127.0.0.1'),match=/^\/([a-z-]+)\/models$/.exec(url.pathname);assert.ok(match);const route=match[1],number=(counts.get(route)??0)+1;counts.set(route,number);assert.deepEqual([...url.searchParams.keys()],number===1?['pageSize']:['pageSize','pageToken']);assert.equal(url.searchParams.get('pageSize'),'1000');
 const json=value=>{response.writeHead(200,{'Content-Type':'application/json'});response.end(JSON.stringify(value));},raw=value=>{response.writeHead(200,{'Content-Type':'application/json'});response.end(value);};
 if(route==='good'){
  if(number===1){json({models:[model('models/gemini-z'),model('models/embedding-only',['embedContent']),{name:'models/no-declared-methods'}],nextPageToken:opaque});return;}
  assert.equal(number,2);assert.equal(url.searchParams.get('pageToken'),opaque);assert.equal(request.url,'/good/models?pageSize=1000&pageToken='+encodeURIComponent(opaque));json({models:[model('models/gemini-a',['embedContent','generateContent'])],nextPageToken:''});return;
 }
 if(route==='empty'){json({models:[]});return;}if(route==='omitted'){json({});return;}if(route==='embedding'){json({models:[model('models/embed',['embedContent']),model('models/unknown',[])]});return;}
 if(route==='repeat'){if(number===1){json({models:[model('models/cycle-a')],nextPageToken:'cycle/+='});return;}assert.equal(number,2);assert.equal(url.searchParams.get('pageToken'),'cycle/+=');raw('{"models":[{"name":"models/cycle-b","supportedGenerationMethods":["generateContent"]}],"nextPageToken":"cycle\\u002F\\u002B\\u003D"}');return;}
 if(route==='duplicate'){json({models:[model('models/duplicate'),model('models/duplicate')]});return;}if(route==='duplicate-json'){raw('{"models":[{"name":"models/a","name":"models/b","supportedGenerationMethods":["generateContent"]}]}');return;}
 if(route==='duplicate-late'){if(number===2)assert.equal(url.searchParams.get('pageToken'),'duplicate');json({models:[model('models/duplicate')],...(number===1?{nextPageToken:'duplicate'}:{})});return;}
 const invalidResources={bare:'gemini-bare',path:'models/has/slash',dots:'models/..',long:'models/'+ 'x'.repeat(129),unicode:'models/雪',reflected:'models/prefix-'+key+'-suffix'};if(Object.hasOwn(invalidResources,route)){json({models:[model(invalidResources[route])]});return;}
 if(route==='bad-models'){json({models:null});return;}if(route==='bad-name'){json({models:[model(7)]});return;}if(route==='bad-method'){json({models:[model('models/a',[7])]});return;}if(route==='bad-cursor'){json({models:[model('models/a')],nextPageToken:7});return;}if(route==='long-cursor'){json({models:[model('models/a')],nextPageToken:'x'.repeat(4097)});return;}if(route==='reflected-cursor'){json({models:[model('models/a')],nextPageToken:'prefix-'+key+'-suffix'});return;}if(route==='bad-json'){raw('{"models": invalid-private-catalogue-body}');return;}
 if(route==='depth'){let extra={};for(let depth=0;depth<17;++depth)extra={nested:extra};json({models:[model('models/a')],extra});return;}
 if(route==='page-overflow'){json({models:Array.from({length:1001},(_,index)=>model('models/page-'+index))});return;}
 if(route==='entry-overflow'){if(number>1)assert.equal(url.searchParams.get('pageToken'),'entry-'+(number-1));assert.ok(number<=5);json({models:Array.from({length:number===5?97:1000},(_,index)=>model(`models/page-${number}-${index}`)),...(number<5?{nextPageToken:'entry-'+number}:{})});return;}
 if(route==='page-limit'){if(number>1)assert.equal(url.searchParams.get('pageToken'),'page-'+(number-1));assert.ok(number<=8);json({models:[model('models/page-'+number)],nextPageToken:'page-'+number});return;}
 if(route==='oversized'){raw('{"models":[],"extra":"'+'x'.repeat(1024*1024)+'"}');return;}
 if(route==='late-invalid'){if(number===1){json({models:[model('models/partial')],nextPageToken:'late'});return;}assert.equal(number,2);assert.equal(url.searchParams.get('pageToken'),'late');json({models:[model('models/final',false)]});return;}
 if(route==='unauthorized'){response.writeHead(401,{'Content-Type':'application/json'});response.end(JSON.stringify({error:{message:'private-catalogue-body'}}));return;}if(route==='redirect'){response.writeHead(307,{Location:'/must-not-arrive/models?pageSize=1000'});response.end();return;}
 assert.ok(route==='cancel'||route==='deadline');held.push(response);
 }catch(error){failure??=error;response.destroy();}});});
try{
 await new Promise(resolve=>peer.listen(0,'127.0.0.1',resolve));let result;try{result=await execute(executable,[`http://127.0.0.1:${peer.address().port}`],{windowsHide:true,timeout:35000});}catch(error){if(failure)throw failure;throw error;}if(failure)throw failure;
 const expected=new Map([['good',2],['empty',1],['omitted',1],['embedding',1],['repeat',2],['duplicate',1],['duplicate-late',2],['duplicate-json',1],['bare',1],['path',1],['dots',1],['long',1],['unicode',1],['reflected',1],['bad-models',1],['bad-name',1],['bad-method',1],['bad-cursor',1],['long-cursor',1],['reflected-cursor',1],['bad-json',1],['depth',1],['page-overflow',1],['entry-overflow',5],['page-limit',8],['oversized',1],['late-invalid',2],['unauthorized',1],['redirect',1],['cancel',1],['deadline',1]]);assert.deepEqual([...counts],[...expected]);process.stdout.write(result.stdout);
}finally{for(const response of held)response.destroy();peer.closeAllConnections();if(peer.listening)await new Promise(resolve=>peer.close(resolve));}
