import {writeFileSync} from 'node:fs';
import {join} from 'node:path';
import {StringDecoder} from 'node:string_decoder';

// Test-fixture output only. No command line, environment or process memory.
// Keeps the original rejection and records both streams even after a timeout.
export async function retainFixtureTiming(pending,{phase,directory}={}){
 if(!directory)return pending;
 if(!/^[a-z][a-z-]{0,31}$/.test(phase))throw new Error('Invalid fixture timing phase');
 const started=performance.now(),startedAtUtc=new Date().toISOString(),timing=[],decoders={stdout:new StringDecoder('utf8'),stderr:new StringDecoder('utf8')};let droppedChunks=0,result,failure;
 const append=(stream,text)=>{if(!text)return;if(timing.length<4096)timing.push({elapsedMs:performance.now()-started,observedAtUtc:new Date().toISOString(),stream,text});else ++droppedChunks;};
 for(const stream of ['stdout','stderr'])pending.child[stream].on('data',bytes=>append(stream,decoders[stream].write(bytes)));
 try{result=await pending;return result;}catch(error){failure=error;throw error;}
 finally{
  for(const stream of ['stdout','stderr'])append(stream,decoders[stream].end());
  const output=failure||result;
  const record={phase,pid:pending.child.pid,startedAtUtc,completedAtUtc:new Date().toISOString(),clockScope:'fixture observer after subprocess spawn; elapsedMs is monotonic',elapsedMs:performance.now()-started,code:failure?(failure.code??null):0,signal:failure?.signal??null,killed:failure?.killed??false,stdout:output?.stdout??'',stderr:output?.stderr??'',timing,droppedChunks,commandLineIncluded:false,environmentIncluded:false};
  try{writeFileSync(join(directory,phase+'-'+process.pid+'.json'),JSON.stringify(record,null,2)+'\n',{flag:'wx'});}
  catch(error){if(!failure)throw error;process.stderr.write('Fixture timing retention failed; original subprocess failure retained.\n');}
 }
}
