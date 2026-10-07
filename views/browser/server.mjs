// Local browser access/view adapter only. Native xMind owns every domain action.
import {createServer} from 'node:http';import {readFile,realpath} from 'node:fs/promises';import {resolve,join} from 'node:path';
import {pathToFileURL} from 'node:url';
import {randomBytes} from 'node:crypto';
const assets={'/ui/':['index.html','text/html; charset=utf-8'],'/ui/browser.js':['browser.js','text/javascript; charset=utf-8'],'/ui/browser.css':['browser.css','text/css; charset=utf-8'],'/ui/chat.js':['chat.js','text/javascript; charset=utf-8'],'/ui/chat.css':['chat.css','text/css; charset=utf-8'],'/ui/client.js':['client.js','text/javascript; charset=utf-8'],'/ui/marked.js':['marked.js','text/javascript; charset=utf-8'],'/ui/purify.js':['purify.js','text/javascript; charset=utf-8']};
const csp="default-src 'none'; script-src 'self'; style-src 'self'; connect-src 'self'; img-src 'self' data:; base-uri 'none'; form-action 'none'; frame-ancestors 'none'; object-src 'none'";
function origin(input){const url=new URL(input);if(url.protocol!=='http:'||!['127.0.0.1','localhost'].includes(url.hostname)||url.username||url.password||url.pathname!=='/'||url.search||url.hash)throw new Error('Use a loopback native backend origin');url.hostname='127.0.0.1';return url.origin;}
function apiPath(path){return /^\/v1\/(?:health|models|graphs|provider\/(?:configuration|models)|sessions(?:\/[A-Za-z0-9_-]+\/(?:history|runs))?|runs(?:\/[A-Za-z0-9_-]+(?:\/(?:events|cancel|operations))?)?|graph-runs(?:\/[A-Za-z0-9_-]+(?:\/(?:children(?:\/[A-Za-z0-9_-]+\/history)?|events|human\/[A-Za-z0-9_.-]+))?)?|operations\/[A-Za-z0-9_-]+(?:\/(?:inspection|decision))?)$/.test(path);}
export async function createBrowserServer({backend,assetRoot}){
 const destination=origin(backend),directory=await realpath(resolve(assetRoot));
 async function readAssets(){const snapshot=new Map();let total=0;for(const [route,[name,mime]] of Object.entries(assets)){const file=await realpath(join(directory,name));if(file!==join(directory,name))throw new Error('Browser assets must remain in the configured directory');const bytes=await readFile(file);total+=bytes.length;if(bytes.length>2*1024*1024||total>8*1024*1024)throw new Error('Browser assets exceed limits');snapshot.set(route,{bytes,mime});}return snapshot;}
 let files=await readAssets();
 let viewOrigin,cookieName;const sessions=new Map(),sessionLifetime=8*60*60*1000;
 const expire=()=>{for(const [id,value] of sessions)if(value.expires<=Date.now())sessions.delete(id);};
 const sessionFor=request=>{expire();const matches=(request.headers.cookie||'').split(';').map(v=>v.trim()).filter(v=>v.startsWith(cookieName+'='));if(matches.length!==1)return;return sessions.get(matches[0].slice(cookieName.length+1));};
 const server=createServer(async(request,response)=>{
  const reply=(status,value)=>{if(!response.headersSent)response.writeHead(status,{'Content-Type':'application/json','Cache-Control':'no-store','X-Content-Type-Options':'nosniff'});response.end(JSON.stringify(value));};
  try{
   if(request.headers.host!==new URL(viewOrigin).host){reply(400,{detail:'Invalid view host'});return;}
   const url=new URL(request.url,viewOrigin);if(url.origin!==viewOrigin){reply(400,{detail:'Invalid request destination'});return;}
   if(request.headers.origin&&request.headers.origin!==viewOrigin||['cross-site','same-site'].includes(request.headers['sec-fetch-site'])){reply(403,{detail:'View origin rejected'});return;}
   if(url.pathname==='/ui/session'||url.pathname==='/ui/session/disconnect'){
    if(url.search||request.headers.origin!==viewOrigin||request.method!=='POST'||request.headers['content-type']!=='application/json'){reply(403,{detail:'Same-origin session access required'});return;}
    let size=0;for await(const chunk of request){size+=chunk.length;if(size>4096){reply(413,{detail:'Session request exceeds limits'});return;}}
    if(url.pathname.endsWith('/disconnect')){const current=sessionFor(request);if(current)for(const [id,value] of sessions)if(value===current)sessions.delete(id);response.setHeader('Set-Cookie',`${cookieName}=; HttpOnly; SameSite=Strict; Path=/; Max-Age=0`);reply(200,{connected:false});return;}
    const authorization=request.headers.authorization;
    if(authorization){
     if(!/^Bearer [\x21-\x7e]{32,256}$/.test(authorization)){reply(401,{detail:'Invalid server access token'});return;}
     const health=await fetch(destination+'/v1/health',{headers:{Authorization:authorization},redirect:'error',signal:AbortSignal.timeout(15000)});
     if(!health.ok){reply(health.status===401?401:502,{detail:'Server access token was not accepted'});return;}
     expire();if(sessions.size>=32){reply(429,{detail:'Too many active browser sessions'});return;}
     const previous=sessionFor(request);if(previous)for(const [id,value] of sessions)if(value===previous)sessions.delete(id);
     const id=randomBytes(32).toString('hex');sessions.set(id,{authorization,expires:Date.now()+sessionLifetime});response.setHeader('Set-Cookie',`${cookieName}=${id}; HttpOnly; SameSite=Strict; Path=/`);reply(200,{connected:true});return;
    }
    const current=sessionFor(request);reply(current?200:401,{connected:!!current});return;
   }
   // A new page load picks up a complete validated view snapshot. UI releases
   // do not restart the access adapter or invalidate its authenticated sessions.
   if(url.pathname==='/ui/'&&request.method==='GET')files=await readAssets();
   const file=files.get(url.pathname);if(file&&(request.method==='GET'||request.method==='HEAD')){response.writeHead(200,{'Content-Type':file.mime,'Content-Security-Policy':csp,'Cache-Control':'no-store','X-Content-Type-Options':'nosniff','X-Frame-Options':'DENY','Referrer-Policy':'no-referrer'});response.end(request.method==='HEAD'?undefined:file.bytes);return;}
   if(!apiPath(url.pathname)||!['GET','POST'].includes(request.method)){reply(404,{detail:'View route not found'});return;}
   if(request.method==='POST'&&request.headers.origin!==viewOrigin){reply(403,{detail:'Same-origin browser access required'});return;}
   let authorization=request.headers.authorization;
   if(!authorization&&request.headers['sec-fetch-site']==='same-origin')authorization=sessionFor(request)?.authorization;
   if(!/^Bearer [\x21-\x7e]{32,256}$/.test(authorization||'')){reply(401,{detail:'Enter the native server access token to connect'});return;}
   if([...url.searchParams.keys()].some(key=>key!=='after')||url.searchParams.getAll('after').length>1||url.searchParams.has('after')&&!/^\d+$/.test(url.searchParams.get('after'))){reply(400,{detail:'Invalid view cursor'});return;}
   if(request.method==='POST'&&request.headers['content-type']!=='application/json'){reply(400,{detail:'Use application/json'});return;}
   const chunks=[];let size=0;for await(const chunk of request){size+=chunk.length;if(size>1024*1024){reply(413,{detail:'Request exceeds limits'});return;}chunks.push(chunk);}
   const controller=new AbortController();response.on('close',()=>{if(!response.writableEnded)controller.abort();});
   // Only the configured native origin receives the supplied bearer. Browser
   // origin enforcement belongs here; native Host/auth guards remain active.
   const upstream=await fetch(destination+url.pathname+url.search,{method:request.method,headers:{Authorization:authorization,...(request.method==='POST'?{'Content-Type':'application/json'}:{})},body:request.method==='POST'?Buffer.concat(chunks):undefined,redirect:'error',signal:AbortSignal.any([controller.signal,AbortSignal.timeout(15000)])});
   if(!(upstream.headers.get('content-type')||'').startsWith('application/json')){reply(502,{detail:'Native backend returned an invalid response'});return;}
   let length=0;const output=[];for await(const chunk of upstream.body){length+=chunk.length;if(length>16*1024*1024){controller.abort();reply(502,{detail:'Native response exceeds view limits'});return;}output.push(chunk);}
   response.writeHead(upstream.status,{'Content-Type':'application/json','Cache-Control':'no-store','X-Content-Type-Options':'nosniff'});response.end(Buffer.concat(output));
  }catch{if(!response.destroyed)reply(502,{detail:'Native backend is unavailable or the request was interrupted'});}
 });server.requestTimeout=20000;server.headersTimeout=10000;server.keepAliveTimeout=1000;
 return {server,listen:async(port=0)=>{await new Promise((yes,no)=>{server.once('error',no);server.listen(port,'127.0.0.1',yes);});viewOrigin='http://127.0.0.1:'+server.address().port;cookieName='xmind_view_'+server.address().port;return viewOrigin;},close:async()=>{sessions.clear();server.closeAllConnections();await new Promise(resolve=>server.close(resolve));}};
}
if(process.argv[1]&&import.meta.url===pathToFileURL(resolve(process.argv[1])).href){const options=new Map();for(let i=2;i<process.argv.length;i+=2){if(!['--backend','--assets','--port'].includes(process.argv[i])||!process.argv[i+1]||options.has(process.argv[i]))throw new Error('Supply --backend ORIGIN --assets DIR [--port PORT]');options.set(process.argv[i],process.argv[i+1]);}const port=Number(options.get('--port')||0);if(!Number.isInteger(port)||port<0||port>65535)throw new Error('Invalid view port');const view=await createBrowserServer({backend:options.get('--backend'),assetRoot:options.get('--assets')});console.log('xMind Browser view listening on '+await view.listen(port)+'/ui/');}
