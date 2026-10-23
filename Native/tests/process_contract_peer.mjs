import {mkdtempSync,mkdirSync,symlinkSync,rmSync,copyFileSync} from 'node:fs';
import {join,resolve,sep} from 'node:path';
import {tmpdir} from 'node:os';
import {spawnSync} from 'node:child_process';
const [binary,node,fixture,xmind,runtimeDll]=process.argv.slice(2);
const root=mkdtempSync(join(tmpdir(),'xmind-native-process-')),workspace=join(root,'workspace'),outside=join(root,'outside');
let passed=false;
try {
  mkdirSync(workspace);mkdirSync(outside);symlinkSync(outside,join(workspace,'junction'),'junction');
  // At 248 characters the image itself fits MAX_PATH, but its adjacent DLL
  // triggers STATUS_NAME_TOO_LONG without an extended-path process launch.
  const loaderRoot=join(root,'loader-'+ 'p'.repeat(Math.max(0,248-root.length-18)));
  mkdirSync(loaderRoot);const loaderImage=join(loaderRoot,'xmind.exe');
  if(loaderImage.length<248)throw new Error('Loader regression path is too short');
  copyFileSync(xmind,loaderImage);copyFileSync(runtimeDll,join(loaderRoot,'xlang3_runtime.dll'));
  const result=spawnSync(binary,[node,fixture,workspace,loaderImage],{encoding:'utf8',windowsHide:true,timeout:30000});
  process.stdout.write(result.stdout||'');process.stderr.write(result.stderr||'');
  if(result.error)throw result.error;if(result.status!==0)throw new Error('Native process adapter contract exit '+result.status);
  passed=true;
} finally {
  const safe=resolve(root),base=resolve(tmpdir())+sep;
  if(!safe.startsWith(base)||!safe.slice(base.length).startsWith('xmind-native-process-'))throw new Error('Unsafe fixture cleanup target');
  if(passed)rmSync(safe,{recursive:true,force:true});
  else console.error(JSON.stringify({retainedFixture:safe}));
}
