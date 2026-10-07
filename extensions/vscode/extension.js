'use strict';
const vscode = require('vscode');
const crypto = require('node:crypto');
const { BackendClient } = require('./client');

function activate(context) {
  let panel;
  let client;
  let sessionId;
  let runId;
  let cursor = 0;
  let timer;
  let polling = false;
  let generation = 0;
  const stateKey = 'agentflow.session';

  const post = message => panel?.webview.postMessage(message);
  const stop = () => { clearInterval(timer); timer = undefined; generation++; };

  async function poll() {
    if (polling || !runId) return;
    polling = true;
    const id = runId;
    const version = generation;
    try {
      const events = await client.events(id, cursor);
      if (version !== generation) return;
      for (const event of events) {
        post({ type: 'event', event });
        cursor = event.seq;
      }
      const run = await client.status(id);
      if (version !== generation) return;
      const approvals = await client.approvals(id);
      if (version !== generation) return;
      post({ type: 'approvals', approvals });
      post({ type: 'status', text: run.status });
      if (['completed', 'cancelled', 'failed'].includes(run.status)) {
        // A terminal transition can occur between the event query and status query.
        const finalEvents = await client.events(id, cursor);
        if (version !== generation) return;
        for (const event of finalEvents) { post({ type: 'event', event }); cursor = event.seq; }
        stop();
      }
    } catch (error) { post({ type: 'error', text: error.message }); }
    finally { polling = false; }
  }

  async function selectSession(id) {
    stop();
    sessionId = id;
    runId = undefined;
    cursor = 0;
    await context.workspaceState.update(stateKey, { url: client.baseUrl, id });
    const history = await client.history(id);
    post({ type: 'history', history });
    const runs = await client.runs(id);
    const latest = runs.at(-1);
    if (latest) {
      runId = latest.id;
      // History already includes completed messages; replay events in a separate log.
      timer = setInterval(poll, 500);
      await poll();
    } else post({ type: 'status', text: 'Ready' });
  }

  async function refresh() {
    const sessions = await client.sessions();
    post({ type: 'sessions', sessions, selected: sessionId });
  }

  function open() {
    if (!vscode.workspace.isTrusted) throw new Error('Trust the workspace before using AgentFlow.');
    if (panel) { panel.reveal(); return; }
    client = new BackendClient(vscode.workspace.getConfiguration('agentflow').get('backendUrl'));
    const saved = context.workspaceState.get(stateKey);
    sessionId = saved?.url === client.baseUrl ? saved.id : undefined;
    panel = vscode.window.createWebviewPanel('agentflow', 'AgentFlow', vscode.ViewColumn.Beside,
      { enableScripts: true, localResourceRoots: [] });
    panel.webview.html = html(crypto.randomBytes(16).toString('hex'));
    panel.onDidDispose(() => { stop(); panel = undefined; }, null, context.subscriptions);
    panel.webview.onDidReceiveMessage(async message => {
      try {
        if (!message || typeof message.type !== 'string') return;
        if (message.type === 'ready') {
          await refresh();
          if (sessionId) await selectSession(sessionId);
        } else if (message.type === 'refresh') await refresh();
        else if (message.type === 'new') {
          const session = await client.createSession('VS Code session');
          await selectSession(session.id);
          await refresh();
        } else if (message.type === 'select' && typeof message.id === 'string') {
          await selectSession(message.id);
        } else if (message.type === 'send' && typeof message.prompt === 'string' && message.prompt.trim()) {
          if (!sessionId) {
            const session = await client.createSession(message.prompt.slice(0, 80));
            await selectSession(session.id);
            await refresh();
          }
          const run = await client.run(sessionId, message.prompt);
          stop(); runId = run.id; cursor = 0;
          post({ type: 'user', text: message.prompt });
          timer = setInterval(poll, 500);
          await poll();
        } else if (message.type === 'cancel' && runId) {
          await client.cancel(runId); await poll();
        } else if (message.type === 'decide' && typeof message.id === 'string' && ['allow', 'deny'].includes(message.decision) && runId) {
          const approvals = await client.approvals(runId);
          if (!approvals.some(approval => approval.id === message.id)) throw new Error('Approval is no longer pending for this run.');
          await client.decide(message.id, message.decision); await poll();
        }
      } catch (error) { post({ type: 'error', text: error.message }); }
    }, null, context.subscriptions);
  }

  context.subscriptions.push(vscode.commands.registerCommand('agentflow.open', () => {
    try { open(); } catch (error) { vscode.window.showErrorMessage(error.message); }
  }));
  context.subscriptions.push(vscode.commands.registerCommand('agentflow.selection', async () => {
    const editor = vscode.window.activeTextEditor;
    if (!editor) return;
    const text = editor.document.getText(editor.selection);
    if (!text) { vscode.window.showInformationMessage('Select code to add it to an AgentFlow prompt.'); return; }
    try {
      open();
      const question = await vscode.window.showInputBox({ prompt: 'Ask about the selected code' });
      if (question) post({ type: 'draft', text: `${question}\n\nFile: ${vscode.workspace.asRelativePath(editor.document.uri)}\n\n${text}` });
    } catch (error) { vscode.window.showErrorMessage(error.message); }
  }));
  context.subscriptions.push({ dispose: stop });
}

function html(nonce) {
  return `<!DOCTYPE html><html><head><meta charset="UTF-8">
  <meta http-equiv="Content-Security-Policy" content="default-src 'none'; style-src 'nonce-${nonce}'; script-src 'nonce-${nonce}';">
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <style nonce="${nonce}">body{font-family:var(--vscode-font-family);color:var(--vscode-foreground);background:var(--vscode-editor-background);padding:16px}button,select,textarea{font:inherit;color:var(--vscode-input-foreground);background:var(--vscode-input-background);border:1px solid var(--vscode-input-border);padding:8px}textarea{width:95%;min-height:100px}pre{white-space:pre-wrap;overflow-wrap:anywhere}#status{margin:12px 0}button{cursor:pointer;margin:4px}</style></head><body>
  <h2>AgentFlow</h2><select id="sessions" aria-label="Session"></select><button id="new">New session</button><button id="refresh">Refresh</button>
  <div id="status" role="status">Connecting…</div><div id="approvals"></div><div id="history"></div>
  <label for="prompt">Prompt</label><textarea id="prompt"></textarea><br><button id="send">Run</button><button id="cancel">Cancel</button>
  <h3>Run events</h3><pre id="events"></pre>
  <script nonce="${nonce}">
  const api=acquireVsCodeApi();const byId=id=>document.getElementById(id);
  for(const type of ['new','refresh','cancel'])byId(type).onclick=()=>api.postMessage({type});
  byId('sessions').onchange=()=>api.postMessage({type:'select',id:byId('sessions').value});
  byId('send').onclick=()=>{api.postMessage({type:'send',prompt:byId('prompt').value});};
  function entry(role,text){const p=document.createElement('pre');p.textContent=role+': '+text;byId('history').append(p);}
  window.addEventListener('message',({data:m})=>{
    if(m.type==='sessions'){byId('sessions').replaceChildren();for(const s of m.sessions){const o=document.createElement('option');o.value=s.id;o.textContent=s.title;o.selected=s.id===m.selected;byId('sessions').append(o);}}
    else if(m.type==='history'){byId('history').replaceChildren();byId('events').textContent='';for(const item of m.history)entry(item.role,item.data.content||JSON.stringify(item.data));}
    else if(m.type==='user'){entry('user',m.text);byId('events').textContent='';byId('prompt').value='';}
    else if(m.type==='draft')byId('prompt').value=m.text;
    else if(m.type==='approvals'){byId('approvals').replaceChildren();for(const a of m.approvals){const box=document.createElement('div');const details=document.createElement('pre');details.textContent='Permission: '+a.tool+'\\n'+JSON.stringify(a.arguments,null,2);box.append(details);for(const decision of ['allow','deny']){const b=document.createElement('button');b.textContent=decision==='allow'?'Allow this operation':'Deny';b.onclick=()=>api.postMessage({type:'decide',id:a.id,decision});box.append(b);}byId('approvals').append(box);}}
    else if(m.type==='event'){byId('events').textContent+=JSON.stringify(m.event)+'\\n';}
    else if(m.type==='status'||m.type==='error')byId('status').textContent=m.text;
  });api.postMessage({type:'ready'});
  </script></body></html>`;
}

module.exports = { activate };
