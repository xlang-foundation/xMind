// Own disposable filesystem fixture for the actual native skill/context code.
import {mkdtemp,realpath,rm,mkdir,writeFile,link,symlink} from 'node:fs/promises';
import {tmpdir} from 'node:os';
import {join,dirname,resolve,basename} from 'node:path';
import {spawnSync} from 'node:child_process';
const parent=await realpath(tmpdir()),folder=await mkdtemp(join(parent,'xmind-skills-'));
try{
 const actual=spawnSync(process.argv[2],[folder],{encoding:'utf8',windowsHide:true,timeout:20000});process.stdout.write(actual.stdout??'');process.stderr.write(actual.stderr??'');if(actual.error)throw actual.error;if(actual.status!==0)throw Error('Native skill fixture failed: '+actual.status);
 const external=join(folder,'outside'),skill=join(folder,'.agents','skills','inspect','SKILL.md');await mkdir(external);await writeFile(join(external,'SKILL.md'),'---\nname: inspect\ndescription: Synthetic linked fixture\n---\nSynthetic outside content must not be exposed.\n');
 const reject=()=>{const result=spawnSync(process.argv[2],[folder,'--catalogue'],{encoding:'utf8',windowsHide:true,timeout:5000});if(result.error)throw result.error;if(result.status===0||result.stdout.includes('Synthetic outside content'))throw Error('Unsafe skill source was accepted');};
 await link(join(external,'SKILL.md'),skill);reject();await rm(skill);
 const agents=join(folder,'.agents'),resolvedAgents=resolve(agents);if(dirname(resolvedAgents)!==resolve(folder))throw Error('Unsafe owned fixture subtree');await rm(resolvedAgents,{recursive:true,force:true});await symlink(external,agents,'junction');reject();await rm(agents);process.stdout.write('Native skill hard-link and directory-junction rejection passed; no live model called\n');
}
finally{const target=resolve(folder);if(dirname(target)!==parent||!basename(target).startsWith('xmind-skills-'))throw Error('Unsafe skill fixture cleanup');await rm(target,{recursive:true,force:true});}
