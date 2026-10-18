import {mkdtempSync,mkdirSync,rmSync,cpSync,lstatSync} from 'node:fs';
import {tmpdir} from 'node:os';
import {resolve,join,sep,basename} from 'node:path';
import {spawnSync} from 'node:child_process';
const [binary,node,fixture,modules,stdlib,powershell,xlang3]=process.argv.slice(2);
if(!powershell||!xlang3)throw new Error('Real PowerShell and paired xlang3 executables are required for the process-profile contracts');
const root=mkdtempSync(join(tmpdir(),'xmind-approved-process-')),workspace=join(root,'workspace');
let succeeded=false;
try {
  mkdirSync(workspace);
  // The CLI child must use this fixture's allowed source, even on machines
  // that happen to have a different library at a runtime fallback location.
  const library=join(root,'configured-pure-library');
  cpSync(stdlib,library,{recursive:true,filter:source=>{const info=lstatSync(source);if(info.isSymbolicLink())throw new Error('Pure-library fixture cannot follow symlinks');return info.isDirectory()?!['__pycache__','site-packages','.git','.config','.agentflow'].includes(basename(source).toLowerCase()):info.isFile()&&source.endsWith('.py');}});
  const result=spawnSync(binary,[node,fixture,workspace,modules,library,powershell,xlang3],{encoding:'utf8',windowsHide:true,timeout:45000});
  process.stdout.write(result.stdout||'');process.stderr.write(result.stderr||'');
  if(result.error)throw result.error;if(result.status!==0)throw new Error('Actual approved native process contract exit '+result.status);
  succeeded=true;
} finally {
  const safe=resolve(root),base=resolve(tmpdir())+sep;
  if(!safe.startsWith(base)||!safe.slice(base.length).startsWith('xmind-approved-process-'))throw new Error('Unsafe fixture cleanup target');
  if(succeeded)rmSync(safe,{recursive:true,force:true});
  else process.stderr.write('Failed native process fixture retained at '+safe+'\n');
}
