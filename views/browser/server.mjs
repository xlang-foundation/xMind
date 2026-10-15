// Local browser access/view adapter only. Native xMind owns every domain action.
import {createServer} from 'node:http';import {readFile,realpath} from 'node:fs/promises';import {resolve,join} from 'node:path';
import {pathToFileURL} from 'node:url';
const assets={'/ui/patch-review.js':['patch-review.js','text/javascript; charset=utf-8'],'/ui/':['index.html','text/html; charset=utf-8'],'/ui/browser.js':['browser.js','text/javascript; charset=utf-8'],'/ui/browser.css':['browser.css','text/css; charset=utf-8'],'/ui/chat.js':['chat.js','text/javascript; charset=utf-8'],'/ui/chat.css':['chat.css','text/css; charset=utf-8'],'/ui/client.js':['client.js','text/javascript; charset=utf-8'],'/ui/marked.js':['marked.js','text/javascript; charset=utf-8'],'/ui/purify.js':['purify.js','text/javascript; charset=utf-8']};
const csp="default-src 'none'; script-src 'self'; style-src 'self'; connect-src 'self'; img-src 'self' data:; base-uri 'none'; form-action 'none'; frame-ancestors 'none'; object-src 'none'";
function origin(input){const url=new URL(input);if(url.protocol!=='http:'||!['127.0.0.1','localhost'].includes(url.hostname)||url.username||url.password||url.pathname!=='/'||url.search||url.hash)throw new Error('Use a loopback native backend origin');url.hostname='127.0.0.1';return url.origin;}
function apiPath(path){return /^\/v1\/sessions\/[A-Za-z0-9_-]+\/(?:model|commands)$/.test(path)||/^\/v1\/vcs\/(?:status|info|branches|base|diff)$/.test(path)||/^\/v1\/(?:health|workspace(?:\/(?:skills|commands|files|file|find|folders\/(?:files|file|find)))?|workspaces|connections|models|process\/profiles|agents(?:\/[A-Za-z0-9_.-]{1,64})?|graphs|mcp\/resources|vcs\/init|a2a\/agents(?:\/register|\/[A-Za-z0-9_.-]{1,64}\/(?:enabled|remove))?|agent\/(?:delegation|planning)|provider\/(?:configuration|models|profiles(?:\/(?:select|models))?)|sessions(?:\/import|\/[A-Za-z0-9_-]+\/(?:history|export|inbox(?:\/[A-Za-z0-9_-]+)?|messages(?:\/[A-Za-z0-9_-]+)?|runs|title|skills|agent|permissions(?:\/[A-Za-z0-9_-]+\/reply)?|context(?:\/(?:compact|requests\/[A-Za-z0-9_-]+))?))?|runs(?:\/[A-Za-z0-9_-]+(?:\/(?:events|tree-events|children(?:\/[A-Za-z0-9_-]+\/history)?|cancel|operations|plan(?:\/(?:human\/[A-Za-z0-9_-]+|resume))?))?)?|graph-runs(?:\/[A-Za-z0-9_-]+(?:\/(?:children(?:\/[A-Za-z0-9_-]+\/history)?|events|human\/[A-Za-z0-9_.-]+|resume))?)?|operations\/[A-Za-z0-9_-]+(?:\/(?:output|inspection|decision|reconcile|a2a\/(?:inspect|resubscribe|cancel|follow-up)))?)$/.test(path);}
export function allowedApiRoute(path,method){
 if(path==='/v1/vcs/init')return method==='POST';
 if(path==='/v1/vcs/status')return method==='GET';
 if(['/v1/vcs/info','/v1/vcs/branches','/v1/vcs/base','/v1/vcs/diff'].includes(path))return method==='GET';
 if(path==='/v1/connections')return method==='GET'||method==='POST';
 if(/^\/v1\/mcp\/authorization\/(?:servers|attempts\/[A-Za-z0-9_-]{1,128})$/.test(path))return method==='GET';
 if(/^\/v1\/mcp\/authorization\/attempts(?:\/[A-Za-z0-9_-]{1,128}\/cancel)?$/.test(path))return method==='POST';
 if(path==='/v1/mcp/authorization/renewals')return method==='POST';
 if(path==='/v1/mcp/resources'||path==='/v1/mcp/resource')return method==='GET';
 if(/^\/v1\/(?:runs\/[A-Za-z0-9_-]+\/(?:events|tree-events)|graph-runs\/[A-Za-z0-9_-]+\/events)\/stream$/.test(path))return method==='GET';
 if(!apiPath(path)||!['GET','POST','DELETE'].includes(method))return false;
 if(path==='/v1/workspaces')return method==='GET';
 if(path==='/v1/a2a/agents')return method==='GET';
 if(path==='/v1/a2a/agents/register'||/^\/v1\/a2a\/agents\/[A-Za-z0-9_.-]{1,64}\/(?:enabled|remove)$/.test(path))return method==='POST';
 if(path==='/v1/process/profiles')return method==='GET';
 if(/^\/v1\/sessions\/[A-Za-z0-9_-]+\/messages(?:\/[A-Za-z0-9_-]+)?$/.test(path))return method==='GET';
 if(/^\/v1\/sessions\/[A-Za-z0-9_-]+\/permissions$/.test(path))return method==='GET';
 if(/^\/v1\/sessions\/[A-Za-z0-9_-]+\/permissions\/[A-Za-z0-9_-]+\/reply$/.test(path))return method==='POST';
 if(/^\/v1\/sessions\/[A-Za-z0-9_-]+\/export$/.test(path))return method==='GET';
 if(path==='/v1/sessions/import')return method==='POST';
 if(/^\/v1\/sessions\/[A-Za-z0-9_-]+\/inbox$/.test(path))return method==='GET'||method==='POST';
 if(path==='/v1/workspace/commands')return method==='GET';
 if(/^\/v1\/sessions\/[A-Za-z0-9_-]+\/commands$/.test(path))return method==='POST';
 if(/^\/v1\/sessions\/[A-Za-z0-9_-]+\/inbox\/[A-Za-z0-9_-]+$/.test(path))return method==='DELETE';
 if(/^\/v1\/operations\/[A-Za-z0-9_-]+\/output$/.test(path))return method==='GET';
 if(/^\/v1\/operations\/[A-Za-z0-9_-]+\/reconcile$/.test(path))return method==='POST';
 if(/^\/v1\/operations\/[A-Za-z0-9_-]+\/a2a\/(?:inspect|resubscribe)$/.test(path))return method==='POST';
 if(/^\/v1\/operations\/[A-Za-z0-9_-]+\/a2a\/cancel$/.test(path))return method==='POST';
 if(/^\/v1\/operations\/[A-Za-z0-9_-]+\/a2a\/follow-up$/.test(path))return method==='POST';
 if(path==='/v1/agents')return method==='GET';
 if(/^\/v1\/agents\/[A-Za-z0-9_.-]{1,64}$/.test(path))return method==='GET';
 if(/^\/v1\/sessions\/[A-Za-z0-9_-]+\/skills$/.test(path))return method==='GET'||method==='POST';
 if(['/v1/workspace/files','/v1/workspace/file','/v1/workspace/find','/v1/workspace/folders/files','/v1/workspace/folders/file','/v1/workspace/folders/find'].includes(path))return method==='GET';
 if(/^\/v1\/sessions\/[A-Za-z0-9_-]+\/(?:agent|model)$/.test(path))return method==='GET'||method==='POST';
 if(/^\/v1\/workspace(?:\/(?:skills|commands))?$/.test(path))return method==='GET';
 if(/^\/v1\/sessions\/[A-Za-z0-9_-]+\/context(?:\/requests\/[A-Za-z0-9_-]+)?$/.test(path))return method==='GET';
 if(/^\/v1\/sessions\/[A-Za-z0-9_-]+\/context\/compact$/.test(path)||/^\/v1\/graph-runs\/[A-Za-z0-9_-]+\/resume$/.test(path))return method==='POST';
 if(/^\/v1\/runs\/[A-Za-z0-9_-]+\/plan\/(?:human\/[A-Za-z0-9_-]+|resume)$/.test(path))return method==='POST';
 if(/^\/v1\/agent\/(?:delegation|planning)$/.test(path)||/^\/v1\/runs\/[A-Za-z0-9_-]+\/(?:tree-events|children(?:\/[A-Za-z0-9_-]+\/history)?|plan)$/.test(path))return method==='GET';
 return true;
}
export async function createBrowserServer({backend,assetRoot,localAccessToken}){
 const destination=origin(backend),directory=await realpath(resolve(assetRoot));
 if(localAccessToken!==undefined&&typeof localAccessToken!=='function'&&!/^[\x21-\x7e]{32,256}$/.test(localAccessToken))throw new Error('Invalid local browser access credential');
 const currentLocalAccessToken=async()=>{const value=typeof localAccessToken==='function'?await localAccessToken():localAccessToken;if(value!==undefined&&(typeof value!=='string'||!/^[\x21-\x7e]{32,256}$/.test(value)))throw new Error('Invalid local browser access credential');return value;};
 async function readAssets(){const snapshot=new Map();let total=0;for(const [route,[name,mime]] of Object.entries(assets)){const file=await realpath(join(directory,name));if(file!==join(directory,name))throw new Error('Browser assets must remain in the configured directory');const bytes=await readFile(file);total+=bytes.length;if(bytes.length>2*1024*1024||total>8*1024*1024)throw new Error('Browser assets exceed limits');snapshot.set(route,{bytes,mime});}return snapshot;}
 let files=await readAssets();
 let viewOrigin,cookieName;
 const sessionFor=request=>{const matches=(request.headers.cookie||'').split(';').map(v=>v.trim()).filter(v=>v.startsWith(cookieName+'='));if(matches.length!==1)return;const value=matches[0].slice(cookieName.length+1);return /^[0-9a-f]{64}\.[0-9a-f]{64}$/.test(value)?value:undefined;};
 const viewHeaders=credential=>({Authorization:'View '+credential,'X-XMind-View-Origin':viewOrigin});
 async function nativeSession(path,headers,body={}){const result=await fetch(destination+'/v1/view-sessions'+path,{method:'POST',headers:{...headers,'Content-Type':'application/json'},body:JSON.stringify(body),redirect:'error',signal:AbortSignal.timeout(15000)});let length=0;const chunks=[];for await(const chunk of result.body){length+=chunk.length;if(length>4096)throw new Error('Session response exceeds limits');chunks.push(chunk);}return {status:result.status,data:JSON.parse(Buffer.concat(chunks).toString('utf8'))};}
 async function nativeJson(path,method,headers,body,timeout=15000){const result=await fetch(destination+path,{method,headers:{...headers,...(method==='POST'?{'Content-Type':'application/json'}:{})},body:method==='POST'?JSON.stringify(body):undefined,redirect:'error',signal:AbortSignal.timeout(timeout)});let length=0;const chunks=[];for await(const chunk of result.body){length+=chunk.length;if(length>1024*1024)throw new Error('Workspace response exceeds limits');chunks.push(chunk);}return {status:result.status,data:JSON.parse(Buffer.concat(chunks).toString('utf8'))};}
 const server=createServer(async(request,response)=>{
  const reply=(status,value)=>{if(!response.headersSent)response.writeHead(status,{'Content-Type':'application/json','Cache-Control':'no-store','X-Content-Type-Options':'nosniff'});response.end(JSON.stringify(value));};
  try{
   if(request.headers.host!==new URL(viewOrigin).host){reply(400,{detail:'Invalid view host'});return;}
   const url=new URL(request.url,viewOrigin);if(url.origin!==viewOrigin){reply(400,{detail:'Invalid request destination'});return;}
   if(request.headers.origin&&request.headers.origin!==viewOrigin||['cross-site','same-site'].includes(request.headers['sec-fetch-site'])){reply(403,{detail:'View origin rejected'});return;}
   if(url.pathname==='/ui/session'||url.pathname==='/ui/session/disconnect'){
    if(url.search||request.headers.origin!==viewOrigin||request.method!=='POST'||request.headers['content-type']!=='application/json'){reply(403,{detail:'Same-origin session access required'});return;}
    let size=0;for await(const chunk of request){size+=chunk.length;if(size>4096){reply(413,{detail:'Session request exceeds limits'});return;}}
    if(url.pathname.endsWith('/disconnect')){const current=sessionFor(request);response.setHeader('Set-Cookie',`${cookieName}=; HttpOnly; SameSite=Strict; Path=/; Max-Age=0`);if(current){const revoked=await nativeSession('/revoke',viewHeaders(current));if(revoked.status!==200&&revoked.status!==401){reply(502,{detail:'View session revocation could not be confirmed'});return;}}reply(200,{connected:false});return;}
    const localCredential=await currentLocalAccessToken();
    const authorization=request.headers.authorization||(localCredential&&!sessionFor(request)?'Bearer '+localCredential:undefined);
    if(authorization){
     if(!/^Bearer [\x21-\x7e]{32,256}$/.test(authorization)){reply(401,{detail:'Local xMind could not authenticate this browser view'});return;}
     const issued=await nativeSession('',{Authorization:authorization},{origin:viewOrigin});
     if(issued.status!==200){reply(issued.status===401?401:issued.status===409?429:502,{detail:issued.status===409?'Too many active browser sessions':'Local xMind could not attach this browser view'});return;}
     const {credential,expires_unix_ms:expires,max_age_seconds:maxAge}=issued.data;
     // Native UTC owns expiry. Use its bounded duration for the browser cookie
     // rather than requiring identical native/Node clock precision.
     if(!/^[0-9a-f]{64}\.[0-9a-f]{64}$/.test(credential)||!Number.isSafeInteger(expires)||expires<1||!Number.isSafeInteger(maxAge)||maxAge<1||maxAge>28800){reply(502,{error_code:'invalid_view_session',detail:'The server returned an invalid access session. Update the server and browser adapter together.'});return;}
     const previous=sessionFor(request);if(previous){const revoked=await nativeSession('/revoke',viewHeaders(previous));if(revoked.status!==200&&revoked.status!==401)throw new Error('Previous view revocation failed');}
     response.setHeader('Set-Cookie',`${cookieName}=${credential}; HttpOnly; SameSite=Strict; Path=/; Max-Age=${maxAge}`);reply(200,{connected:true});return;
    }
    const current=sessionFor(request);if(!current){reply(401,{connected:false});return;}const restored=await nativeSession('/current',viewHeaders(current));if(restored.status===200){reply(200,{connected:true});return;}if(restored.status!==401||!localCredential){reply(restored.status===401?401:502,{connected:false});return;}
    // A native restart can invalidate an old browser cookie while this local
    // adapter still owns the launch credential. Recover silently so a page
    // refresh never asks the user to copy a server token.
    const issued=await nativeSession('',{Authorization:'Bearer '+localCredential},{origin:viewOrigin});if(issued.status!==200){reply(issued.status===401?401:issued.status===409?429:502,{detail:issued.status===409?'Too many active browser sessions':'Local xMind could not restore this browser view'});return;}const {credential,expires_unix_ms:expires,max_age_seconds:maxAge}=issued.data;if(!/^[0-9a-f]{64}\.[0-9a-f]{64}$/.test(credential)||!Number.isSafeInteger(expires)||expires<1||!Number.isSafeInteger(maxAge)||maxAge<1||maxAge>28800){reply(502,{error_code:'invalid_view_session',detail:'The server returned an invalid access session. Update the server and browser adapter together.'});return;}response.setHeader('Set-Cookie',`${cookieName}=${credential}; HttpOnly; SameSite=Strict; Path=/; Max-Age=${maxAge}`);reply(200,{connected:true});return;
   }
   if(url.pathname==='/ui/workspaces'||url.pathname==='/ui/workspaces/select'||url.pathname==='/ui/workspaces/create'||url.pathname==='/ui/workspaces/add'||url.pathname==='/ui/workspaces/pick'){
    const selecting=url.pathname.endsWith('/select'),creating=url.pathname.endsWith('/create'),adding=url.pathname.endsWith('/add'),picking=url.pathname.endsWith('/pick'),mutating=selecting||creating||adding||picking;
    const originMatches=request.headers.origin===viewOrigin,originAbsent=request.headers.origin===undefined;
    if(url.search||(mutating?!originMatches:!originAbsent&&!originMatches)||request.method!==(mutating?'POST':'GET')||(mutating&&request.headers['content-type']!=='application/json')){reply(403,{detail:'Same-origin workspace access required'});return;}
    const credential=sessionFor(request);if(!credential){reply(401,{detail:'Connect to local xMind on this computer first'});return;}
    let body={};if(mutating){let raw='';for await(const chunk of request){raw+=chunk.toString('utf8');if(Buffer.byteLength(raw)>4096){reply(413,{detail:'Workspace request exceeds limits'});return;}}try{body=JSON.parse(raw);}catch{reply(400,{detail:'Invalid workspace request'});return;}const valid=selecting?body&&!Array.isArray(body)&&Object.keys(body).length===1&&typeof body.workspace_id==='string'&&/^windows-local-file-v1:[A-Za-z0-9:._-]{1,200}$/.test(body.workspace_id):picking?body&&!Array.isArray(body)&&Object.keys(body).length===0:adding?body&&!Array.isArray(body)&&Object.keys(body).length===2&&typeof body.workspace_id==='string'&&/^windows-local-file-v1:[A-Za-z0-9:._-]{1,200}$/.test(body.workspace_id)&&typeof body.root==='string'&&body.root.length>0&&body.root.length<=32760:body&&!Array.isArray(body)&&Object.keys(body).length===1&&typeof body.root==='string'&&body.root.length>0&&body.root.length<=32760;if(!valid){reply(400,{detail:selecting?'Select a workspace from the native catalogue':picking?'The local folder picker requires an empty request':adding?'Choose one folder to attach to the selected workspace':'Enter one absolute workspace folder path'});return;}}
    const nativePath='/v1/workspaces'+(selecting?'/select':creating?'/create':adding?'/add':picking?'/pick':''),selected=await nativeJson(nativePath,mutating?'POST': 'GET',viewHeaders(credential),mutating?body:undefined,mutating?120000:15000);
    if(!mutating&&(selected.status===401||selected.status===404)){
     // A VS Code-attached xmind serve owns exactly its opened folder and has no
     // profile catalogue. Read that authenticated workspace as a one-item,
     // non-addable catalogue; xmind view continues to supply the full local
     // profile catalogue and workspace-creation endpoint.
     const current=await nativeJson('/v1/workspace','GET',viewHeaders(credential));
     const workspace=current.data;
     if(current.status===200&&workspace?.configured===true&&typeof workspace.root==='string'&&workspace.root&&typeof workspace.workspace_id==='string'&&/^windows-local-file-v1:[A-Za-z0-9:._-]{1,200}$/.test(workspace.workspace_id)){
      const name=workspace.root.replace(/[\\/]+$/,'').split(/[\\/]/).at(-1)||workspace.root;
      reply(200,{can_add:false,selected_workspace_id:workspace.workspace_id,workspaces:[{workspace_id:workspace.workspace_id,name,root:workspace.root}]});return;
     }
     reply(current.status,current.data);return;
    }
    if(selected.status!==200){reply(selected.status,selected.data);return;}
    if(!mutating){reply(200,selected.data);return;}
    if(picking){const data=selected.data;if(!data||typeof data.cancelled!=='boolean'||data.cancelled&&data.root!==null||!data.cancelled&&(typeof data.root!=='string'||!data.root||data.root.length>32760||/[\u0000-\u001f]/.test(data.root))){reply(502,{detail:'The native host returned an invalid folder selection.'});return;}reply(200,data);return;}
    if(adding){const group=selected.data;if(!group||group.workspace_id!==body.workspace_id||typeof group.root!=='string'||!Array.isArray(group.folders)||group.folders.length<2||!Number.isSafeInteger(group.revision)){reply(502,{detail:'The native host returned an invalid workspace membership.'});return;}reply(200,group);return;}
    const {credential:replacement,expires_unix_ms:expires,max_age_seconds:maxAge,workspace}=selected.data;
    if(!/^[0-9a-f]{64}\.[0-9a-f]{64}$/.test(replacement)||!Number.isSafeInteger(expires)||expires<1||!Number.isSafeInteger(maxAge)||maxAge<1||maxAge>28800||!workspace||workspace.configured!==true||typeof workspace.root!=='string'||!workspace.root||typeof workspace.workspace_id!=='string'||!/^windows-local-file-v1:[A-Za-z0-9:._-]{1,200}$/.test(workspace.workspace_id)||typeof workspace.authority_id!=='string'||!/^[a-f0-9]{32}$/.test(workspace.authority_id)){reply(502,{error_code:'invalid_workspace_session',detail:'The native host returned an invalid workspace session.'});return;}
    response.setHeader('Set-Cookie',`${cookieName}=${replacement}; HttpOnly; SameSite=Strict; Path=/; Max-Age=${maxAge}`);reply(200,{connected:true,workspace});return;
   }
   // A new page load picks up a complete validated view snapshot. UI releases
   // do not restart the access adapter or invalidate its authenticated sessions.
   if(url.pathname==='/ui/'&&request.method==='GET')files=await readAssets();
   const file=files.get(url.pathname);if(file&&(request.method==='GET'||request.method==='HEAD')){response.writeHead(200,{'Content-Type':file.mime,'Content-Security-Policy':csp,'Cache-Control':'no-store','X-Content-Type-Options':'nosniff','X-Frame-Options':'DENY','Referrer-Policy':'no-referrer'});response.end(request.method==='HEAD'?undefined:file.bytes);return;}
   if(!allowedApiRoute(url.pathname,request.method)){reply(404,{detail:'View route not found'});return;}
   if(request.method==='POST'&&request.headers.origin!==viewOrigin){reply(403,{detail:'Same-origin browser access required'});return;}
   let access;const authorization=request.headers.authorization;
   if(/^Bearer [\x21-\x7e]{32,256}$/.test(authorization||''))access={Authorization:authorization};
   else if(!authorization&&request.headers['sec-fetch-site']==='same-origin'){const credential=sessionFor(request);if(credential)access=viewHeaders(credential);}
   if(!access){reply(401,{detail:'Local xMind access is unavailable. Open the browser view from xMind or VS Code.'});return;}
   const contextRead=/^\/v1\/sessions\/[A-Za-z0-9_-]+\/context(?:\/requests\/[A-Za-z0-9_-]+)?$/.test(url.pathname);
   const sessionMessages=/^\/v1\/sessions\/[A-Za-z0-9_-]+\/messages$/.test(url.pathname),sessionMessage=/^\/v1\/sessions\/[A-Za-z0-9_-]+\/messages\/[A-Za-z0-9_-]+$/.test(url.pathname),historyParams=url.searchParams;
   const historyQueryInvalid=sessionMessages&&(request.url.includes('?')&&!historyParams.size||[...historyParams.keys()].some(key=>!['limit','before'].includes(key))||historyParams.getAll('limit').length>1||historyParams.has('limit')&&(!/^[1-9]\d{0,3}$/.test(historyParams.get('limit'))||Number(historyParams.get('limit'))>1000)||historyParams.getAll('before').length>1||historyParams.has('before')&&!/^msg_[1-9]\d*$/.test(historyParams.get('before')||'')||historyParams.has('before')&&!Number.isSafeInteger(Number(historyParams.get('before').slice(4))));
   const workspaceBrowse=url.pathname==='/v1/workspace/files'||url.pathname==='/v1/workspace/file',workspaceFind=url.pathname==='/v1/workspace/find',workspaceFolderFind=url.pathname==='/v1/workspace/folders/find',workspaceFolderBrowse=url.pathname==='/v1/workspace/folders/files'||url.pathname==='/v1/workspace/folders/file';
   const processOutput=/^\/v1\/operations\/[A-Za-z0-9_-]+\/output$/.test(url.pathname),outputParams=url.searchParams,outputAfter=outputParams.get('after'),outputLimit=outputParams.get('limit');
   const processOutputQueryInvalid=outputParams.size!==3||[...outputParams.keys()].some(key=>!['channel','after','limit'].includes(key))||outputParams.getAll('channel').length!==1||!['stdout','stderr'].includes(outputParams.get('channel'))||outputParams.getAll('after').length!==1||! /^(?:0|[1-9]\d*)$/.test(outputAfter||'')||!Number.isSafeInteger(Number(outputAfter))||Number(outputAfter)>64*1024*1024||outputParams.getAll('limit').length!==1||! /^(?:[1-9]\d*)$/.test(outputLimit||'')||!Number.isSafeInteger(Number(outputLimit))||Number(outputLimit)>262144;
   const queryInvalid=sessionMessages?historyQueryInvalid:sessionMessage?request.url.includes('?'):workspaceBrowse?([...url.searchParams.keys()].some(key=>key!=='path')||url.searchParams.getAll('path').length>1||url.searchParams.has('path')&&(!url.searchParams.get('path')||url.searchParams.get('path').length>32768)):
    workspaceFind?([...url.searchParams.keys()].some(key=>!['query','path','type','limit','hidden','respect_ignore'].includes(key))||url.searchParams.getAll('query').length!==1||!url.searchParams.get('query')||url.searchParams.get('query').length>4096||url.searchParams.getAll('path').length>1||url.searchParams.has('path')&&(!url.searchParams.get('path')||url.searchParams.get('path').length>32768)||url.searchParams.getAll('type').length>1||url.searchParams.has('type')&&!['file','directory'].includes(url.searchParams.get('type'))||url.searchParams.getAll('limit').length>1||url.searchParams.has('limit')&&!/^(?:[1-9]\d{0,2}|1000)$/.test(url.searchParams.get('limit'))||['hidden','respect_ignore'].some(key=>url.searchParams.getAll(key).length>1||url.searchParams.has(key)&&!['true','false'].includes(url.searchParams.get(key)))):
    workspaceFolderFind?([...url.searchParams.keys()].some(key=>!['folder_id','query','path','type','limit','hidden','respect_ignore'].includes(key))||url.searchParams.getAll('folder_id').length!==1||!/^windows-local-file-v1:[A-Za-z0-9:._-]{1,200}$/.test(url.searchParams.get('folder_id')||'')||url.searchParams.getAll('query').length!==1||!url.searchParams.get('query')||url.searchParams.get('query').length>4096||url.searchParams.getAll('path').length>1||url.searchParams.has('path')&&(!url.searchParams.get('path')||url.searchParams.get('path').length>32768)||url.searchParams.getAll('type').length>1||url.searchParams.has('type')&&!['file','directory'].includes(url.searchParams.get('type'))||url.searchParams.getAll('limit').length>1||url.searchParams.has('limit')&&!/^(?:[1-9]\d{0,2}|1000)$/.test(url.searchParams.get('limit'))||['hidden','respect_ignore'].some(key=>url.searchParams.getAll(key).length>1||url.searchParams.has(key)&&!['true','false'].includes(url.searchParams.get(key)))):
    workspaceFolderBrowse?([...url.searchParams.keys()].some(key=>key!=='folder_id'&&key!=='path')||url.searchParams.getAll('folder_id').length!==1||!/^windows-local-file-v1:[A-Za-z0-9:._-]{1,200}$/.test(url.searchParams.get('folder_id')||'')||url.searchParams.getAll('path').length>1||url.searchParams.has('path')&&(!url.searchParams.get('path')||url.searchParams.get('path').length>32768)||url.pathname.endsWith('/file')&&!url.searchParams.has('path')):
    contextRead?[...url.searchParams.keys()].some(key=>key!=='model_id')||url.searchParams.getAll('model_id').length>1||url.searchParams.has('model_id')&&!/^[A-Za-z0-9_.:/-]{1,256}$/.test(url.searchParams.get('model_id')):
    url.pathname==='/v1/mcp/resource'?([...url.searchParams.keys()].some(key=>key!=='location[directory]')||url.searchParams.getAll('location[directory]').length>1||url.searchParams.has('location[directory]')&&(!url.searchParams.get('location[directory]')||url.searchParams.get('location[directory]').length>32768)):
    processOutput?processOutputQueryInvalid:
    (request.method==='POST'||url.pathname.startsWith('/v1/mcp/authorization/')||url.pathname==='/v1/mcp/resources'||url.pathname==='/v1/mcp/resource'||url.pathname==='/v1/agent/planning'||/^\/v1\/runs\/[A-Za-z0-9_-]+\/plan(?:\/.*)?$/.test(url.pathname))&&request.url.includes('?')||[...url.searchParams.keys()].some(key=>key!=='after')||url.searchParams.getAll('after').length>1||url.searchParams.has('after')&&!/^\d+$/.test(url.searchParams.get('after'));
   if(queryInvalid){reply(400,{detail:'Invalid view observation query'});return;}
   if(request.method==='POST'&&request.headers['content-type']!=='application/json'){reply(400,{detail:'Use application/json'});return;}
   const chunks=[];let size=0;const requestLimit=url.pathname==='/v1/sessions/import'?4*1024*1024:1024*1024;for await(const chunk of request){size+=chunk.length;if(size>requestLimit){reply(413,{detail:'Request exceeds limits'});return;}chunks.push(chunk);}
   const controller=new AbortController();response.on('close',()=>{if(!response.writableEnded)controller.abort();});
   // Only the configured native origin receives the supplied access credential. Browser
   // origin enforcement belongs here; native Host/auth guards remain active.
   const streaming=/^\/v1\/(?:runs\/[A-Za-z0-9_-]+\/(?:events|tree-events)|graph-runs\/[A-Za-z0-9_-]+\/events)\/stream$/.test(url.pathname),timeoutMs=streaming?30000:url.pathname.endsWith('/a2a/follow-up')?45000:15000;
   const upstream=await fetch(destination+url.pathname+url.search,{method:request.method,headers:{...access,...(request.method==='POST'?{'Content-Type':'application/json'}:{}),...(streaming&&request.headers['last-event-id']?{'Last-Event-ID':request.headers['last-event-id']}:{})},body:request.method==='POST'?Buffer.concat(chunks):undefined,redirect:'error',signal:AbortSignal.any([controller.signal,AbortSignal.timeout(timeoutMs)])});
   if(streaming&&upstream.status===200){
    if(upstream.headers.get('content-type')!=='text/event-stream'){controller.abort();reply(502,{detail:'Invalid native event response'});return;}
    response.writeHead(200,{'Content-Type':'text/event-stream','Cache-Control':'no-store','X-Content-Type-Options':'nosniff','X-Accel-Buffering':'no'});response.flushHeaders();
    let total=0;for await(const chunk of upstream.body){total+=chunk.length;if(total>64*1024*1024)throw new Error('Event stream exceeds limits');if(!response.write(chunk))await new Promise((yes,no)=>{const cleanup=()=>{response.off('drain',drain);response.off('close',closed);},drain=()=>{cleanup();yes();},closed=()=>{cleanup();no(new Error('View detached'));};response.once('drain',drain);response.once('close',closed);});}response.end();return;
   }
   if(upstream.status===204){response.writeHead(204,{'Cache-Control':'no-store','X-Content-Type-Options':'nosniff'});response.end();return;}
   if(!(upstream.headers.get('content-type')||'').startsWith('application/json')){reply(502,{detail:'Native backend returned an invalid response'});return;}
   let length=0;const output=[];for await(const chunk of upstream.body){length+=chunk.length;if(length>16*1024*1024){controller.abort();reply(502,{detail:'Native response exceeds view limits'});return;}output.push(chunk);}
   response.writeHead(upstream.status,{'Content-Type':'application/json','Cache-Control':'no-store','X-Content-Type-Options':'nosniff'});response.end(Buffer.concat(output));
  }catch{if(!response.destroyed){if(response.headersSent)response.destroy();else reply(502,{detail:'Native backend is unavailable or the request was interrupted'});}}
 });server.requestTimeout=20000;server.headersTimeout=10000;server.keepAliveTimeout=1000;
 return {server,listen:async(port=0)=>{await new Promise((yes,no)=>{server.once('error',no);server.listen(port,'127.0.0.1',yes);});viewOrigin='http://127.0.0.1:'+server.address().port;cookieName='xmind_view_'+server.address().port;return viewOrigin;},close:async()=>{server.closeAllConnections();await new Promise(resolve=>server.close(resolve));}};
}
if(process.argv[1]&&import.meta.url===pathToFileURL(resolve(process.argv[1])).href){const options=new Map();for(let i=2;i<process.argv.length;i+=2){if(!['--backend','--assets','--port'].includes(process.argv[i])||!process.argv[i+1]||options.has(process.argv[i]))throw new Error('Supply --backend ORIGIN --assets DIR [--port PORT]');options.set(process.argv[i],process.argv[i+1]);}const port=Number(options.get('--port')||0);if(!Number.isInteger(port)||port<0||port>65535)throw new Error('Invalid view port');const view=await createBrowserServer({backend:options.get('--backend'),assetRoot:options.get('--assets')});console.log('xMind Browser view listening on '+await view.listen(port)+'/ui/');}
