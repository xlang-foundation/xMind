'use strict';

// Browser terminal view over the same native, conversation-bound xMind PTY
// used by the console and VS Code clients. It does not create a shell locally.
class BrowserTerminalView {
  constructor({client,session,host,Terminal,FitAddon,current=()=>true,onStatus=()=>{},onFinish=()=>{}}){
    if(!client||typeof session!=='string'||!host||typeof Terminal!=='function'||typeof FitAddon!=='function')throw new Error('Invalid browser terminal dependencies');
    this.client=client;this.session=session;this.host=host;this.Terminal=Terminal;this.FitAddon=FitAddon;this.current=current;this.onStatus=onStatus;this.onFinish=onFinish;
    this.sequence=0;this.id=undefined;this.closed=false;this.finished=false;this.timer=undefined;this.delay=100;this.inputQueue=Promise.resolve();this.inputBytes=0;this.resizeQueue=Promise.resolve();this.decoder=new TextDecoder('utf-8');
  }
  async open(){
    this.terminal=new this.Terminal({allowProposedApi:false,cursorBlink:true,convertEol:false,fontFamily:'Consolas, "Cascadia Mono", monospace',fontSize:13,scrollback:5000,theme:{background:'#11161d',foreground:'#e2e6ed',cursor:'#9bb8ff',selectionBackground:'#3b5277'}});
    this.fitAddon=new this.FitAddon();this.terminal.loadAddon(this.fitAddon);this.terminal.open(this.host);this.fit();
    this.inputSubscription=this.terminal.onData(data=>this.input(data));this.resizeSubscription=this.terminal.onResize(size=>this.resize(size));
    this.resizeObserver=typeof ResizeObserver==='function'?new ResizeObserver(()=>this.fit()):undefined;this.resizeObserver?.observe(this.host);
    try{
      const size=this.dimensions();const terminal=await this.client.createTerminal(this.session,'xMind Browser Terminal',size.rows,size.columns);
      if(this.closed||!this.current()){await this.client.closeTerminal(terminal.id).catch(()=>{});this.finish();return;}
      this.id=terminal.id;this.sequence=terminal.output_sequence;this.onStatus('Connected to the selected conversation terminal.');this.terminal.write(`xMind workspace terminal · ${terminal.title}\r\n`);this.pump();
    }catch(error){if(!this.closed){this.writeError('Terminal could not start',error);this.finish();}}
  }
  dimensions(){const size=this.fitAddon?.proposeDimensions?.();return{rows:Number.isSafeInteger(size?.rows)?Math.max(5,Math.min(300,size.rows)):24,columns:Number.isSafeInteger(size?.cols)?Math.max(20,Math.min(500,size.cols)):80};}
  fit(){if(this.closed||!this.fitAddon)return;try{this.fitAddon.fit();}catch{} }
  input(data){
    if(this.closed||!this.id||!this.current()||typeof data!=='string'||!data)return;
    const bytes=new TextEncoder().encode(data).length;if(bytes>65536||this.inputBytes+bytes>1024*1024){this.terminal.write('\r\n\x1b[33m[xMind input queue full; input discarded]\x1b[0m\r\n');return;}
    this.inputBytes+=bytes;const id=this.id;this.inputQueue=this.inputQueue.then(async()=>{if(!this.closed&&this.id===id&&this.current())await this.client.terminalInput(id,data);}).catch(error=>{if(!this.closed&&this.current())this.writeError('Terminal input failed',error);}).finally(()=>{this.inputBytes=Math.max(0,this.inputBytes-bytes);});
  }
  resize(size){
    if(this.closed||!this.id||!this.current()||!Number.isSafeInteger(size?.rows)||!Number.isSafeInteger(size?.cols))return;
    const rows=Math.max(5,Math.min(300,size.rows)),columns=Math.max(20,Math.min(500,size.cols)),id=this.id;
    this.resizeQueue=this.resizeQueue.then(async()=>{if(!this.closed&&this.id===id&&this.current())await this.client.resizeTerminal(id,rows,columns);}).catch(error=>{if(!this.closed&&this.current())this.writeError('Terminal resize failed',error);});
  }
  async pump(){
    if(this.closed||!this.id)return;if(!this.current()){this.close();return;}const id=this.id;
    try{
      const output=await this.client.terminalOutput(id,this.sequence);if(this.closed||this.id!==id||!this.current())return;this.sequence=output.next_sequence;
      if(output.truncated)this.terminal.write('\r\n\x1b[33m[xMind output before the retained terminal window was truncated]\x1b[0m\r\n');
      const bytes=new Uint8Array(output.data.length/2);for(let index=0;index<bytes.length;index++)bytes[index]=Number.parseInt(output.data.slice(index*2,index*2+2),16);
      const text=this.decoder.decode(bytes,{stream:true});if(text)this.terminal.write(text);this.delay=100;
      if(!output.running){const tail=this.decoder.decode();if(tail)this.terminal.write(tail);this.terminal.write(`\r\n\x1b[90m[xMind terminal exited with code ${output.exit_code}]\x1b[0m\r\n`);this.finish();return;}
    }catch(error){if(this.closed||this.id!==id)return;if(!this.current()){this.close();return;}this.onStatus('Terminal observation paused; reconnecting.');this.delay=Math.min(2000,this.delay*2);}
    this.timer=setTimeout(()=>{this.timer=undefined;this.pump();},this.delay);
  }
  writeError(label,error){const message=String(error?.message||'backend unavailable').replace(/[\x00-\x1f\x7f]/g,' ').slice(0,240);this.terminal?.write(`\r\n\x1b[31m[xMind ${label}: ${message}]\x1b[0m\r\n`);this.onStatus(message);}
  async close(){if(this.closed)return;this.closed=true;clearTimeout(this.timer);this.resizeObserver?.disconnect();this.inputSubscription?.dispose();this.resizeSubscription?.dispose();const id=this.id;this.id=undefined;if(id)await this.client.closeTerminal(id).catch(()=>{});this.finish();}
  finish(){if(this.finished)return;this.finished=true;this.closed=true;clearTimeout(this.timer);this.resizeObserver?.disconnect();this.inputSubscription?.dispose();this.resizeSubscription?.dispose();this.terminal?.dispose();this.onFinish(this);}
}

if(typeof module!=='undefined'&&module.exports)module.exports={BrowserTerminalView};
else globalThis.XMindBrowserTerminal={BrowserTerminalView};
