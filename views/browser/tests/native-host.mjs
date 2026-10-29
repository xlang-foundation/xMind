// Isolated ASGI fixture launcher, never a Node web server. Production starts
// xmind web; native integration peers may supply an external fixture endpoint.
import {spawn} from 'node:child_process';
import {resolve,join,dirname} from 'node:path';
import {fileURLToPath} from 'node:url';
import {createRequire} from 'node:module';
import {once} from 'node:events';
const root=resolve(dirname(fileURLToPath(import.meta.url)),'../../..');
const require=createRequire(import.meta.url);
export const {allowedApiRoute}=require('../routes.cjs');
export async function createNativeBrowserFixture({backend,assetRoot,localAccessToken,modules,stdlib,productionAsync=false}){
  let child,origin,closed=false;
  const runtime=process.env.XMIND_BROWSER_TEST_RUNTIME;
  const config={backend,assets:resolve(assetRoot),web_source:join(root,'views/browser'),production_async:productionAsync,runtime,
    modules:modules||process.env.XMIND_BROWSER_TEST_MODULES||(runtime&&join(runtime,'modules')),
    stdlib:stdlib||process.env.XMIND_BROWSER_TEST_STDLIB||(runtime&&join(runtime,'stdlib')),
    packages:process.env.XMIND_BROWSER_TEST_PACKAGES};
  if(!config.modules||!config.stdlib||!config.packages)throw new Error('Configure the embedded-xlang3 ASGI fixture runtime and pip packages; CPython is not supported.');
  const executable=process.env.XMIND_BROWSER_TEST_HOST||join(root,'build/skill-state-native/Release/xmind_browser_asgi_fixture.exe');
  return {
    async listen(port=0){
      if(child||closed)throw new Error('Native fixture already started or closed');
      const owner=typeof localAccessToken==='function'?await localAccessToken():localAccessToken;
      if(closed)throw new Error('Native fixture closed before owner readiness');
      const env={...process.env};for(const key of Object.keys(env))if(key.startsWith('XMIND_UI_')||['XMIND_API_KEY','XMIND_AUTH_TOKEN','XMIND_VIEW_TOKEN'].includes(key))delete env[key];
      child=spawn(executable,[],{windowsHide:true,stdio:['pipe','pipe','pipe'],env});
      let output='',error='';child.stderr.on('data',bytes=>{if(error.length<8192)error+=bytes;});
      const ready=new Promise((yes,no)=>{child.once('error',no);child.once('exit',code=>no(new Error('Native ASGI fixture exited '+code+': '+error)));child.stdout.on('data',bytes=>{output+=bytes;if(output.length>16384){no(new Error('Invalid native fixture metadata'));return;}if(output.includes('\n'))try{origin=JSON.parse(output.split('\n')[0]).origin;yes();}catch(failure){no(failure);}});});
      child.stdin.end(JSON.stringify({...config,owner,port})+'\n');await ready;
      const deadline=Date.now()+15000;
      while(child.exitCode===null){try{const response=await fetch(origin+'/ui/',{signal:AbortSignal.timeout(1000)});if(response.status===200)return origin;}catch{}if(Date.now()>=deadline)throw new Error('Native fixture readiness unresolved: '+error);await new Promise(yes=>setTimeout(yes,10));}
      throw new Error('Native fixture failed: '+error);
    },
    async close(){closed=true;if(child&&child.exitCode===null){const exit=once(child,'exit');child.kill();await exit;}},
    get processId(){return child?.pid;}
  };
}
