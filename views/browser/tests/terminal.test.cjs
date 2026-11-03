'use strict';
const test=require('node:test'),assert=require('node:assert/strict');
const {BrowserTerminalView}=require('../terminal.js');

class FakeTerminal{
 constructor(options={}){this.output='';this.disposed=false;this.options={...options};}
 loadAddon(addon){addon.activate?.(this);}
 open(){}
 onData(callback){this.input=callback;return{dispose(){}};}
 onResize(callback){this.resizeEvent=callback;return{dispose(){}};}
 write(value){this.output+=value;}
 dispose(){this.disposed=true;}
}
class FakeFit{activate(){}fit(){}proposeDimensions(){return{rows:31,cols:101};}}
const terminal=id=>({id,session_id:'session-one',title:'xMind Browser Terminal',rows:31,columns:101,running:true,exit_code:null,output_sequence:4});
const until=async check=>{for(let i=0;i<100;i++){if(check())return;await new Promise(resolve=>setTimeout(resolve,2));}throw new Error('Timed out waiting for browser terminal event');};

test('browser terminal creates a session-bound native PTY and relays UTF-8, input, resize and close',async()=>{
 const calls=[],host={};let polls=0;const client={
  async createTerminal(...args){calls.push(['create',...args]);return terminal('a'.repeat(32));},
  async terminalOutput(id,after){calls.push(['output',id,after]);polls++;return polls===1?{encoding:'hex',data:'e99baa',next_sequence:7,truncated:false,running:true,exit_code:null}:{encoding:'hex',data:'',next_sequence:7,truncated:false,running:true,exit_code:null};},
  async terminalInput(id,data){calls.push(['input',id,data]);},async resizeTerminal(id,rows,columns){calls.push(['resize',id,rows,columns]);},async closeTerminal(id){calls.push(['close',id]);}
 };
 const view=new BrowserTerminalView({client,session:'session-one',host,Terminal:FakeTerminal,FitAddon:FakeFit});await view.open();await until(()=>view.terminal.output.includes('雪'));
 assert.deepEqual(calls[0],['create','session-one','xMind Browser Terminal',31,101]);assert.equal(view.terminal.output,'xMind workspace terminal · xMind Browser Terminal\r\n雪');
 view.terminal.input('npm run build\r');view.terminal.resizeEvent({rows:42,cols:120});await until(()=>calls.some(call=>call[0]==='input')&&calls.some(call=>call[0]==='resize'));await view.close();
 assert.ok(calls.some(call=>call[0]==='input'&&call[2]==='npm run build\r'));assert.ok(calls.some(call=>call[0]==='resize'&&call[2]===42&&call[3]===120));assert.ok(calls.some(call=>call[0]==='close'&&call[1]==='a'.repeat(32)));assert.equal(view.terminal.disposed,true);
});

test('browser terminal refuses a late PTY after its conversation admission is revoked',async()=>{
 let allowed=true,closed=0,output=0,resolveCreate;const client={createTerminal(){return new Promise(resolve=>resolveCreate=resolve);},async closeTerminal(){closed++;},async terminalOutput(){output++;throw new Error('must not observe a revoked terminal');}};
 const view=new BrowserTerminalView({client,session:'session-one',host:{},Terminal:FakeTerminal,FitAddon:FakeFit,current:()=>allowed});const opening=view.open();await until(()=>typeof resolveCreate==='function');allowed=false;resolveCreate(terminal('b'.repeat(32)));await opening;assert.equal(closed,1);assert.equal(output,0);assert.equal(view.terminal.disposed,true);
});

test('normal terminal exit retains output and status until explicit close, without more native input or polling',async()=>{
 const calls=[],statuses=[];let finished=0;
 const client={
  async createTerminal(){return terminal('c'.repeat(32));},
  async terminalOutput(){calls.push('output');return{encoding:'hex',data:Buffer.from('XMIND_EXIT_OUTPUT 雪\r\n').toString('hex'),next_sequence:40,truncated:false,running:false,exit_code:7};},
  async terminalInput(){calls.push('input');},async resizeTerminal(){calls.push('resize');},async closeTerminal(){calls.push('close');}
 };
 const view=new BrowserTerminalView({client,session:'session-one',host:{},Terminal:FakeTerminal,FitAddon:FakeFit,onStatus:text=>statuses.push(text),onFinish:()=>finished++});
 try{
  await view.open();await until(()=>view.terminal.output.includes('exited with code 7'));
  assert.match(view.terminal.output,/XMIND_EXIT_OUTPUT 雪/);assert.equal(view.terminal.disposed,false,'Keep the completed terminal visible');
  assert.equal(view.terminal.options.disableStdin,true);assert.equal(view.terminal.options.cursorBlink,false);
  assert.equal(statuses.at(-1),'Terminal exited with code 7.');assert.equal(finished,0,'The host hides the panel only on disposal');
  view.input('must not execute\r');view.resize({rows:30,cols:90});await Promise.all([view.inputQueue,view.resizeQueue]);
  await new Promise(resolve=>setTimeout(resolve,120));assert.deepEqual(calls,['output']);
  await view.close();await view.close();assert.deepEqual(calls,['output','close']);assert.equal(view.terminal.disposed,true);assert.equal(finished,1);
 }finally{await view.close();}
});

test('terminal startup failure stays visible until explicit close without a native resource to close',async()=>{
 let finished=0,closed=0;
 const client={async createTerminal(){throw new Error('fixture startup refused');},async closeTerminal(){closed++;}};
 const view=new BrowserTerminalView({client,session:'session-one',host:{},Terminal:FakeTerminal,FitAddon:FakeFit,onFinish:()=>finished++});
 try{await view.open();assert.match(view.terminal.output,/Terminal could not start: fixture startup refused/);assert.equal(view.terminal.disposed,false);assert.equal(view.terminal.options.disableStdin,true);assert.equal(finished,0);}
 finally{await view.close();}
 assert.equal(view.terminal.disposed,true);assert.equal(finished,1);assert.equal(closed,0);
});
