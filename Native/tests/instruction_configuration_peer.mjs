// Own temporary storage for an actual native/embedded-xlang3 contract.
import {mkdtempSync,rmSync} from 'node:fs';
import {join,resolve,dirname,basename} from 'node:path';
import {tmpdir} from 'node:os';
import {spawnSync} from 'node:child_process';
const [binary,modules,stdlib]=process.argv.slice(2),root=mkdtempSync(join(tmpdir(),'xmind-instructions-'));
try{const result=spawnSync(binary,[root,modules,stdlib],{encoding:'utf8',timeout:30000,windowsHide:true});process.stdout.write(result.stdout||'');process.stderr.write(result.stderr||'');if(result.error)throw result.error;if(result.status!==0)throw new Error('Native instruction contract exit '+result.status);}
finally{const safe=resolve(root);if(dirname(safe)!==resolve(tmpdir())||!basename(safe).startsWith('xmind-instructions-'))throw new Error('Unsafe instruction fixture cleanup target');rmSync(safe,{recursive:true,force:true});}
