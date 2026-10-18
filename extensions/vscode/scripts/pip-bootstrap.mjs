// File-only extraction of ensurepip's bundled pure wheel. No Python execution.
import {fromBufferPromise} from 'yauzl';
import {createHash} from 'node:crypto';
import runtime from '../native-runtime.js';
const need=(condition,message)=>{if(!condition)throw new Error(message);};
export async function pipBootstrap(wheel,name){
  const version=/^pip-([0-9]+(?:\.[0-9]+){1,3})-py3-none-any\.whl$/.exec(name)?.[1];
  need(version&&wheel.length<=8*1024*1024,'A bounded bundled pure pip wheel is required.');
  const distribution=`pip-${version}.dist-info`,files=new Map(),names=new Set();let total=0;
  const archive=await fromBufferPromise(wheel,{lazyEntries:true,strictFileNames:true,validateEntrySizes:true});
  try{for await(const entry of archive.eachEntry()){
    const source=entry.fileName;if(source.endsWith('/'))continue;
    need(runtime.relativeFile(source)&&!names.has(source.toLowerCase())&&(entry.externalFileAttributes>>>16&0xf000)!==0xa000,'Unsafe or duplicate pip wheel member.');names.add(source.toLowerCase());
    const metadata=source.startsWith(distribution+'/'),packageFile=source.startsWith('pip/')&&runtime.runtimeFileKind('stdlib/'+source);
    need((metadata||packageFile)&&runtime.runtimeFileKind('stdlib/'+source)&&entry.uncompressedSize<=8*1024*1024&&names.size<=2048,'Unsupported pip wheel member: '+source.slice(0,200));
    const stream=await archive.openReadStreamPromise(entry),chunks=[];let length=0;
    for await(const chunk of stream){length+=chunk.length;total+=chunk.length;need(length<=8*1024*1024&&total<=32*1024*1024,'Pip bootstrap source exceeds its bound.');chunks.push(chunk);}
    need(length===entry.uncompressedSize,'Pip wheel member size changed.');files.set('stdlib/'+source,Buffer.concat(chunks));
  }}finally{archive.close();}
  const text=name=>files.get('stdlib/'+distribution+'/'+name)?.toString('utf8');
  need(/^Name: pip\r?$/m.test(text('METADATA')||'')&&new RegExp('^Version: '+version.replaceAll('.','\\.')+'\\r?$','m').test(text('METADATA')||'')&&/^Root-Is-Purelib: true\r?$/mi.test(text('WHEEL')||'')&&/^Tag: py3-none-any\r?$/m.test(text('WHEEL')||''),'Pip bootstrap distribution metadata differs from its pure wheel.');
  need(runtime.REQUIRED_BOOTSTRAP.every(name=>files.has('stdlib/'+name))&&[...files.keys()].some(name=>name.startsWith('stdlib/'+distribution+'/licenses/')),'Pip bootstrap sources, TLS data or license notices are incomplete.');
  const records=new Map();for(const line of (text('RECORD')||'').trim().split(/\r?\n/)){const fields=line.split(',');need(fields.length===3&&!records.has(fields[0]),'Invalid pip wheel RECORD.');records.set(fields[0],fields.slice(1));}
  need(records.size===files.size,'Pip wheel RECORD inventory differs.');
  for(const [target,value]of files){const source=target.slice(7),record=records.get(source);need(record,'Pip wheel RECORD is missing a member.');if(source===distribution+'/RECORD')need(record[0]===''&&record[1]==='','Pip wheel RECORD cannot hash itself.');else need(record[0]==='sha256='+createHash('sha256').update(value).digest('base64url')&&record[1]===String(value.length),'Pip wheel RECORD content differs.');}
  return {files,metadata:{version,source:'ensurepip/_bundled/'+name,wheelSha256:createHash('sha256').update(wheel).digest('hex'),files:files.size}};
}
