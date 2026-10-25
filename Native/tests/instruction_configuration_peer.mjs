// Retain failed native fixtures; successful synthetic storage may be removed.
import {mkdtempSync,rmSync,writeFileSync} from 'node:fs';
import {join,resolve,dirname,basename} from 'node:path';
import {tmpdir} from 'node:os';
import {execFile} from 'node:child_process';
import {promisify} from 'node:util';
import {withOwnedProcessDiagnostics} from '../../Tools/owned-process-diagnostics.mjs';
const [binary,modules,stdlib]=process.argv.slice(2),root=mkdtempSync(join(tmpdir(),'xmind-instructions-'));
const launchedAt=new Date().toISOString();
let succeeded=false;
try{
  const operation=promisify(execFile)(binary,[root,modules,stdlib],{encoding:'utf8',timeout:30000,windowsHide:true});
  const diagnosticOptions=process.env.XMIND_INSTRUCTION_DIAGNOSTICS==='1'?{enabled:true,captureStacks:true,evidenceRoot:process.env.XMIND_INSTRUCTION_DIAGNOSTIC_DIR}:{};
  const result=await withOwnedProcessDiagnostics(operation,binary,{label:'instruction-configuration',captureOwner:true,...diagnosticOptions});
  process.stdout.write(result.stdout||'');process.stderr.write(result.stderr||'');succeeded=true;
}catch(error){
  process.stdout.write(error.stdout||'');process.stderr.write(error.stderr||'');
  writeFileSync(join(root,'failure.json'),JSON.stringify({launchedAt,failedAt:new Date().toISOString(),binary:resolve(binary),deadlineMs:30000,code:error.code,signal:error.signal,killed:error.killed,stdout:error.stdout||'',stderr:error.stderr||''},null,2)+'\n',{flag:'wx'});
  process.stderr.write('Failed instruction fixture retained at '+root+'\n');throw error;
}finally{
  const safe=resolve(root);if(dirname(safe)!==resolve(tmpdir())||!basename(safe).startsWith('xmind-instructions-'))throw new Error('Unsafe instruction fixture cleanup target');
  if(succeeded)rmSync(safe,{recursive:true,force:true});
}
