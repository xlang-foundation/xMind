// Independent actual child process fixture; never loaded by the product.
import {writeFileSync,existsSync} from 'node:fs';
import {spawn} from 'node:child_process';
const [mode,marker,...args]=process.argv.slice(2);
if(mode==='normal') {
  writeFileSync(marker,'actual child effect');
  process.stdout.write(JSON.stringify({args,cwd:process.cwd(),configured:process.env.XMIND_PROCESS_FIXTURE,
    inherited:!!(process.env.XMIND_AUTH_TOKEN||process.env.XMIND_API_KEY||process.env.XMIND_PARENT_ONLY)}));
  process.stderr.write('stderr 🌍');
} else if(mode==='bytes') {
  process.stdout.write(Buffer.from([255,0,254,10]));process.stderr.write(Buffer.from([128,13,0]));process.exitCode=7;
} else if(mode==='flood') {
  await Promise.all([new Promise(resolve=>process.stdout.write(Buffer.alloc(512*1024,111),resolve)),
    new Promise(resolve=>process.stderr.write(Buffer.alloc(512*1024,101),resolve))]);
  writeFileSync(marker,'both channels drained');
} else if(mode==='tree'||mode==='parent-exit') {
  const code='const fs=require("node:fs");setTimeout(()=>fs.writeFileSync(process.argv[1],"unexpected surviving child"),5000);setInterval(()=>{},1000)';
  const child=spawn(process.execPath,['-e',code,marker],{stdio:['ignore','inherit','inherit'],windowsHide:true});
  process.stdout.write(JSON.stringify({descendant:child.pid})+'\n',()=>{
    if(mode==='parent-exit')process.exit(0);
  });
  if(mode==='tree')setInterval(()=>{},1000);
} else if(mode==='handle') {
  process.stdout.write('READY\n');
  const tick=setInterval(()=>{
    if(!existsSync(args[0]))return;
    clearInterval(tick);writeFileSync(marker,'unrelated handle was not inherited');
  },10);
} else if(mode==='input-eof') {
  let size=0;process.stdin.on('data',b=>size+=b.length);process.stdin.on('end',()=>{
    writeFileSync(marker,'input EOF');process.stdout.write(JSON.stringify({size}));
  });process.stdin.resume();
} else throw new Error('Unknown native process fixture mode');
