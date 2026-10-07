// Read-only inspection of this task's real VS Code development host.
import {readFile,writeFile} from 'node:fs/promises';
const preview=process.argv[2]||'ui-host';if(!/^[A-Za-z0-9_-]{1,64}$/.test(preview))throw new Error('Invalid named preview');
const metadata=JSON.parse(await readFile(new URL('../.agentflow/'+preview+'/active.json',import.meta.url),'utf8'));
const debugPort=metadata.debug_port||(preview==='ui-host'?57217:undefined);if(!Number.isInteger(debugPort)||debugPort<1024||debugPort>65535)throw new Error('Named preview has no valid debug port');
const targets=await(await fetch('http://127.0.0.1:'+debugPort+'/json/list')).json();
const target=targets.find(value=>value.type==='page'&&value.title.includes('Extension Development Host'));
if(!target)throw new Error('Development host is not running');
const socket=new WebSocket(target.webSocketDebuggerUrl),pending=new Map();let seq=0;
await new Promise((resolve,reject)=>{socket.addEventListener('open',resolve,{once:true});socket.addEventListener('error',reject,{once:true});});
socket.addEventListener('message',event=>{const result=JSON.parse(event.data);if(result.id&&pending.has(result.id)){const {resolve,reject}=pending.get(result.id);pending.delete(result.id);result.error?reject(new Error(result.error.message)):resolve(result.result);}});
function call(method,params={}){const id=++seq;return new Promise((resolve,reject)=>{pending.set(id,{resolve,reject});socket.send(JSON.stringify({id,method,params}));});}
try {
  const geometry=await call('Runtime.evaluate',{expression:`JSON.stringify({width:innerWidth,height:innerHeight,sidebar:[...document.querySelectorAll('.part.sidebar,.part.auxiliarybar')].map(el=>{const r=el.getBoundingClientRect();return {class:el.className,x:r.x,width:r.width,visible:r.width>0,title:el.innerText.slice(0,160)}})})`,returnByValue:true});
  console.log(geometry.result.value);
  const screenshot=await call('Page.captureScreenshot',{format:'png'});
  await writeFile(new URL('../.agentflow/'+preview+'/sidebar.png',import.meta.url),Buffer.from(screenshot.data,'base64'));
  console.log('Actual development-host screenshot saved');
} finally {socket.close();}
