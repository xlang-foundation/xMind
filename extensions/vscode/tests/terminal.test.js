'use strict';
const test=require('node:test');
const assert=require('node:assert/strict');
const {XMindPseudoterminal}=require('../terminal');

class EventEmitter {
  constructor(){this.listeners=[];this.event=listener=>{this.listeners.push(listener);return{dispose:()=>{this.listeners=this.listeners.filter(value=>value!==listener);}};};}
  fire(value){for(const listener of this.listeners)listener(value);}
}
const vscode={EventEmitter};
const until=async check=>{for(let i=0;i<100;i++){if(check())return;await new Promise(resolve=>setTimeout(resolve,5));}throw new Error('Timed out waiting for terminal event');};

test('VS Code pseudoterminal binds terminal, input and resize to one saved conversation',async()=>{
 const id='c'.repeat(32),calls=[],writes=[],closed=[];let poll=0;
 const client={
  async createTerminal(...args){calls.push(['create',...args]);return{id,session_id:'session-one',title:'xMind Terminal',rows:30,columns:100,running:true,exit_code:null,output_sequence:0};},
  async terminalOutput(terminal,after){calls.push(['output',terminal,after]);poll++;return{encoding:'hex',data:poll===1?'c3':'a9',next_sequence:after+1,truncated:false,running:true,exit_code:null};},
  async terminalInput(terminal,data){calls.push(['input',terminal,data]);},
  async resizeTerminal(terminal,rows,columns){calls.push(['resize',terminal,rows,columns]);},
  async closeTerminal(terminal){calls.push(['close',terminal]);}
 };
 const pty=new XMindPseudoterminal(vscode,client,'session-one');pty.onDidWrite(value=>writes.push(value));pty.onDidClose(value=>closed.push(value));
 pty.open({rows:30,columns:100});
 await until(()=>writes.includes('é'));
 pty.handleInput('dir\r');pty.setDimensions({rows:40,columns:120});
 await until(()=>calls.some(call=>call[0]==='input')&&calls.some(call=>call[0]==='resize'));
 pty.close();
 await until(()=>calls.some(call=>call[0]==='close'));
 assert.deepEqual(calls.find(call=>call[0]==='create'),['create','session-one','xMind Terminal',30,100]);
 assert.ok(calls.filter(call=>call[0]==='input').every(call=>call[1]===id));
 assert.ok(calls.filter(call=>call[0]==='resize').every(call=>call[1]===id));
 assert.deepEqual(closed,[undefined]);
});

test('VS Code pseudoterminal refuses backend output after workspace admission is revoked',async()=>{
 let current=true,created=0,closed=0;const writes=[];
 const client={createTerminal:async()=>{created++;return{id:'d'.repeat(32),session_id:'session-one',title:'xMind Terminal',rows:24,columns:80,running:true,exit_code:null,output_sequence:0};},terminalOutput:async()=>{throw new Error('must not poll after revocation');},closeTerminal:async()=>{closed++;}};
 const pty=new XMindPseudoterminal(vscode,client,'session-one',{current:()=>current});pty.onDidWrite(value=>writes.push(value));
 pty.open();await until(()=>created===1);current=false;await until(()=>closed===1);
 assert.equal(writes.length,1,'only the local terminal title is written before admission revocation');
 assert.match(writes[0],/xMind workspace terminal/);
});
