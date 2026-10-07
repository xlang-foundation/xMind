'use strict';
const vscode = require('vscode');
const crypto = require('node:crypto');
const { BackendClient, backendOrigin, validateToken } = require('./client');

function activate(context) {
  let panel;
  let client;
  let sessionId;
  let runId;
  let cursor = 0;
  let timer;
  let polling = false;
  let generation = 0;
  let opening;
  let messages = Promise.resolve();
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
      post({ type: 'status', text: run.state });
      if (['completed', 'cancelled', 'failed'].includes(run.state)) {
        // A terminal transition can occur between the event query and status query.
        const finalEvents = await client.events(id, cursor);
        if (version !== generation) return;
        for (const event of finalEvents) { post({ type: 'event', event }); cursor = event.seq; }
        const history = await client.history(sessionId);
        if (version !== generation) return;
        post({ type: 'transcript', history });
        stop();
      }
    } catch (error) { if (version === generation) post({ type: 'error', text: error.message }); }
    finally { polling = false; }
  }

  async function selectSession(id) {
    stop();
    const version = generation;
    sessionId = id;
    runId = undefined;
    cursor = 0;
    await context.workspaceState.update(stateKey, { url: client.baseUrl, id });
    if (version !== generation || !panel) return;
    const history = await client.history(id);
    if (version !== generation || !panel) return;
    post({ type: 'history', history });
    const runs = await client.runs(id);
    if (version !== generation || !panel) return;
    const latest = runs.at(-1);
    if (latest) {
      runId = latest.id;
      // History already includes completed messages; replay events in a separate log.
      timer = setInterval(poll, 500);
      await poll();
    } else post({ type: 'status', text: 'Ready' });
  }

  async function refresh() {
    const version = generation;
    const sessions = await client.sessions();
    if (version !== generation || !panel) return;
    post({ type: 'sessions', sessions, selected: sessionId });
  }

  function configuredOrigin() {
    return backendOrigin(vscode.workspace.getConfiguration('agentflow').get('backendUrl'));
  }
  const secretKey = origin => `xmind.auth:${origin}`;
  async function configureToken() {
    if (!vscode.workspace.isTrusted) throw new Error('Trust the workspace before connecting to xMind Server.');
    const origin = configuredOrigin();
    const token = await vscode.window.showInputBox({
      title: 'xMind Server authentication', password: true, ignoreFocusOut: true,
      prompt: `Enter the XMIND_AUTH_TOKEN for ${origin}. This is the server access token.`,
      validateInput: value => { try { validateToken(value); return undefined; } catch (error) { return error.message; } }
    });
    if (token === undefined) return false;
    await context.secrets.store(secretKey(origin), validateToken(token));
    return true;
  }
  async function openPanel() {
    if (!vscode.workspace.isTrusted) throw new Error('Trust the workspace before using AgentFlow.');
    if (panel) { panel.reveal(); return; }
    await messages;
    const origin = configuredOrigin();
    if (!await context.secrets.get(secretKey(origin)) && !await configureToken()) return;
    client = new BackendClient(origin, () => context.secrets.get(secretKey(origin)));
    const health = await client.health();
    const saved = context.workspaceState.get(stateKey);
    sessionId = saved?.url === client.baseUrl ? saved.id : undefined;
    panel = vscode.window.createWebviewPanel('agentflow', 'xMind', vscode.ViewColumn.Beside,
      { enableScripts: true, localResourceRoots: [] });
    panel.webview.html = html(crypto.randomBytes(16).toString('hex'));
    const view = panel;
    panel.onDidDispose(() => { if (panel === view) { stop(); panel = undefined; } }, null, context.subscriptions);
    panel.webview.onDidReceiveMessage(message => {
      // Serialize view commands so overlapping selections/submissions cannot
      // overwrite the observed session or display one session's response in another.
      messages = messages.then(async () => { try {
        if (panel !== view) return;
        if (!message || typeof message.type !== 'string') return;
        if (message.type === 'ready') {
          post({ type: 'capabilities', execution: health.agent_execution });
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
          if (!health.agent_execution) throw new Error('Configure a model on xMind Server before submitting an agent run.');
          if (!sessionId) {
            const session = await client.createSession(message.prompt.slice(0, 80));
            if (panel !== view) return;
            await selectSession(session.id);
            await refresh();
          }
          if (panel !== view) return;
          const run = await client.run(sessionId, message.prompt);
          if (panel !== view) return; // Accepted backend execution survives view closure.
          stop(); runId = run.id; cursor = 0;
          post({ type: 'user', text: message.prompt });
          timer = setInterval(poll, 500);
          await poll();
        } else if (message.type === 'cancel' && runId) {
          await client.cancel(runId); await poll();
        }
      } catch (error) { if (panel === view) post({ type: 'error', text: error.message }); } });
    }, null, context.subscriptions);
  }
  function open() {
    if (!opening) opening = openPanel().finally(() => { opening = undefined; });
    return opening;
  }

  context.subscriptions.push(vscode.commands.registerCommand('agentflow.open', async () => {
    try { await open(); } catch (error) { vscode.window.showErrorMessage(error.message); }
  }));
  context.subscriptions.push(vscode.commands.registerCommand('agentflow.configureToken', async () => {
    try { await configureToken(); } catch (error) { vscode.window.showErrorMessage(error.message); }
  }));
  context.subscriptions.push(vscode.commands.registerCommand('agentflow.selection', async () => {
    const editor = vscode.window.activeTextEditor;
    if (!editor) return;
    const text = editor.document.getText(editor.selection);
    if (!text) { vscode.window.showInformationMessage('Select code to add it to an AgentFlow prompt.'); return; }
    try {
      await open();
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
  <h2>xMind</h2><select id="sessions" aria-label="Session"></select><button id="new">New session</button><button id="refresh">Refresh</button>
  <div id="status" role="status">Connecting…</div><div id="history"></div>
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
    else if(m.type==='history'||m.type==='transcript'){byId('history').replaceChildren();if(m.type==='history')byId('events').textContent='';for(const item of m.history)entry(item.role,item.data.content||JSON.stringify(item.data));}
    else if(m.type==='capabilities'){byId('send').disabled=!m.execution;if(!m.execution)byId('status').textContent='Connect a model on xMind Server to run an agent.';}
    else if(m.type==='user'){entry('user',m.text);byId('events').textContent='';byId('prompt').value='';}
    else if(m.type==='draft')byId('prompt').value=m.text;
    else if(m.type==='event'){byId('events').textContent+=JSON.stringify(m.event)+'\\n';}
    else if(m.type==='status'||m.type==='error')byId('status').textContent=m.text;
  });api.postMessage({type:'ready'});
  </script></body></html>`;
}

module.exports = { activate };
