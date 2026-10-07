'use strict';
const vscode = require('vscode');
const crypto = require('node:crypto');
const { BackendClient, backendOrigin, validateToken } = require('./client');
const { html } = require('./webview');
const { editReview } = require('./edit-review');

async function activate(context) {
  // Interactive preview uses a normal development host. VS Code test hosts
  // deliberately use in-memory storage and cannot verify reconnect persistence.
  let previewReady,previewOrigin;
  if(typeof process!=='undefined' && context.extensionMode===vscode.ExtensionMode?.Development && process.env.XMIND_UI_BACKEND_ORIGIN && process.env.XMIND_UI_READY_FILE) {
    previewOrigin=backendOrigin(process.env.XMIND_UI_BACKEND_ORIGIN);previewReady=process.env.XMIND_UI_READY_FILE;
    const token=process.env.XMIND_UI_BOOTSTRAP_TOKEN;
    delete process.env.XMIND_UI_BOOTSTRAP_TOKEN;delete process.env.XMIND_UI_BACKEND_ORIGIN;delete process.env.XMIND_UI_READY_FILE;
    if(!vscode.workspace.isTrusted)throw new Error('Trust the workspace before connecting to xMind Server.');
    await vscode.workspace.getConfiguration('agentflow').update('backendUrl',previewOrigin,vscode.ConfigurationTarget.Global);
    if(token)await context.secrets.store(`xmind.auth:${previewOrigin}`,validateToken(token));
  }
  const showEditReview=editReview(vscode,context);
  let panel;
  let sidebarView;
  let resolveSidebar;
  let client;
  let sessionId;
  let runId;
  let cursor = 0;
  let timer;
  let polling = false;
  let generation = 0;
  let opening;
  let messages = Promise.resolve();
  let reviewed = new Map();
  let modelCatalogue = {models:[],default_model:''};
  let selectedModel;
  const stateKey = 'agentflow.session';
  const modelStateKey = 'xmind.model';

  const post = message => panel?.webview.postMessage(message);
  const stop = () => { clearInterval(timer); timer = undefined; generation++; };
  context.subscriptions.push(vscode.window.registerWebviewViewProvider('xmind.workspace', {
    resolveWebviewView(view) {
      sidebarView = view;
      if (resolveSidebar) { const resolve = resolveSidebar; resolveSidebar = undefined; resolve(view); }
      else open().catch(error => vscode.window.showErrorMessage(error.message));
    }
  }, { webviewOptions: { retainContextWhenHidden: true } }));
  async function acquireSidebar() {
    if (sidebarView) return sidebarView;
    const available = new Promise(resolve => { resolveSidebar = resolve; });
    await vscode.commands.executeCommand('workbench.view.extension.xmind');
    await vscode.commands.executeCommand('xmind.workspace.focus');
    return available;
  }

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
      if (events.some(event => ['conversation.assistant','conversation.tool_turn'].includes(event.kind))) {
        const history = await client.history(sessionId);
        if (version !== generation) return;
        post({ type:'transcript',history,preserveLive:true });
      }
      const run = await client.status(id);
      if (version !== generation) return;
      post({ type: 'status', text: run.state });
      const operations = await client.operations(id);
      if (version !== generation) return;
      reviewed = new Map(operations.map(operation => [operation.id, operation]));
      post({ type: 'operations', operations });
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
    reviewed.clear();
    post({ type: 'operations', operations: [] });
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

  async function capabilities() {
    const health=await client.health();let catalogue={models:[],default_model:''};
    if(health.agent_execution) {
      try {catalogue=await client.models();}
      catch(error) {
        if(error.status!==404) throw error;
        if(health.model) catalogue={default_model:health.model,models:[{id:health.model}]};
      }
    }
    return {health,catalogue};
  }
  function chooseModel(catalogue,preferred) {
    return catalogue.models.some(model=>model.id===preferred)?preferred:catalogue.default_model||undefined;
  }

  function configuredOrigin() {
    return backendOrigin(vscode.workspace.getConfiguration('agentflow').get('backendUrl'));
  }
  const secretKey = origin => `xmind.auth:${origin}`;
  async function configureToken(initialToken) {
    if (!vscode.workspace.isTrusted) throw new Error('Trust the workspace before connecting to xMind Server.');
    const origin = configuredOrigin();
    const token = typeof initialToken === 'string' ? validateToken(initialToken) : await vscode.window.showInputBox({
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
    if (panel) { panel.show(); return; }
    await messages;
    const origin = configuredOrigin();
    const bootstrapToken = typeof process !== 'undefined' ? process.env.XMIND_UI_BOOTSTRAP_TOKEN : undefined;
    if (!await context.secrets.get(secretKey(origin)) && !await configureToken(bootstrapToken)) return;
    client = new BackendClient(origin, () => context.secrets.get(secretKey(origin)));
    const initial=await capabilities();let health=initial.health;modelCatalogue=initial.catalogue;
    const savedModel=context.workspaceState.get(modelStateKey);
    selectedModel=chooseModel(modelCatalogue,savedModel?.url===client.baseUrl?savedModel.id:undefined);
    const saved = context.workspaceState.get(stateKey);
    sessionId = saved?.url === client.baseUrl ? saved.id : undefined;
    panel = await acquireSidebar();
    panel.webview.options = { enableScripts: true, localResourceRoots: [context.extensionUri] };
    const asset = (...parts) => panel.webview.asWebviewUri(vscode.Uri.joinPath(context.extensionUri, ...parts)).toString();
    panel.webview.html = html(crypto.randomBytes(16).toString('hex'), {
      source:panel.webview.cspSource,css:asset('media','chat.css'),script:asset('media','chat.js'),
      marked:asset('node_modules','marked','lib','marked.umd.js'),purify:asset('node_modules','dompurify','dist','purify.min.js')
    });
    const view = panel;
    panel.onDidDispose(() => { if (panel === view) { stop(); reviewed.clear(); panel = undefined; sidebarView = undefined; } }, null, context.subscriptions);
    panel.webview.onDidReceiveMessage(message => {
      // Selection intent invalidates an in-flight approval immediately, before
      // its queued selection handler can run behind that network request.
      if (panel === view && ['select','new','refresh'].includes(message?.type)) {
        stop(); reviewed.clear(); post({ type: 'operations', operations: [] });
      }
      // Serialize view commands so overlapping selections/submissions cannot
      // overwrite the observed session or display one session's response in another.
      messages = messages.then(async () => { try {
        if (panel !== view) return;
        if (!message || typeof message.type !== 'string') return;
        if (message.type === 'ready') {
          post({ type: 'capabilities', execution: health.agent_execution, model:selectedModel, models:modelCatalogue.models });
          await refresh();
          if (sessionId) await selectSession(sessionId);
        } else if (message.type === 'refresh') {
          const version=generation;
          post({type:'capabilities',execution:false,models:[],model:undefined});
          post({type:'status',text:'Reconnecting to xMind…'});
          const current=await capabilities();
          if(panel!==view || version!==generation) return;
          health=current.health;modelCatalogue=current.catalogue;
          selectedModel=chooseModel(modelCatalogue,selectedModel);
          post({type:'capabilities',execution:health.agent_execution,models:modelCatalogue.models,model:selectedModel});
          await refresh();
          if(panel!==view || version!==generation) return;
          if(sessionId) await selectSession(sessionId);
          else if(health.agent_execution) post({type:'status',text:'Ready'});
        }
        else if (message.type === 'model' && typeof message.id === 'string') {
          if (!modelCatalogue.models.some(model => model.id === message.id)) throw new Error('Model is not configured on this backend.');
          selectedModel = message.id;
          await context.workspaceState.update(modelStateKey,{url:client.baseUrl,id:selectedModel});
        }
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
          const run = await client.run(sessionId, message.prompt, selectedModel);
          if (panel !== view) return; // Accepted backend execution survives view closure.
          stop(); runId = run.id; cursor = 0;
          reviewed.clear(); post({ type: 'operations', operations: [] });
          post({ type: 'user', text: message.prompt });
          timer = setInterval(poll, 500);
          await poll();
        } else if (message.type === 'cancel' && runId) {
          await client.cancel(runId); await poll();
        } else if (message.type === 'copy' && typeof message.text === 'string' && message.text.length <= 1048576) {
          await vscode.env.clipboard.writeText(message.text);
        } else if (message.type === 'openLink' && typeof message.url === 'string') {
          const link = new URL(message.url);
          if (!['https:','http:'].includes(link.protocol)) throw new Error('Only HTTP/HTTPS links can be opened.');
          await vscode.env.openExternal(vscode.Uri.parse(link.href));
        } else if (message.type === 'inspect-edit' && typeof message.id === 'string') {
          if (!vscode.workspace.isTrusted) throw new Error('Trust the workspace before inspecting an edit.');
          const proposal=reviewed.get(message.id);
          if(!proposal || proposal.run_id!==runId || proposal.state!=='uncertain' || proposal.tool!=='replace_file') throw new Error('Select a recorded uncertain file edit before inspecting.');
          const version=generation;
          const inspection=await client.inspectEdit(proposal.id);
          if(panel!==view || version!==generation) return;
          const current=inspection.operation,observed=inspection.observed;
          if(!current || current.id!==proposal.id || current.state!=='uncertain' || current.run_id!==proposal.run_id || current.workspace_id!==proposal.workspace_id || current.tool!==proposal.tool || current.arguments_json!==proposal.arguments_json || current.expires_unix_ms!==proposal.expires_unix_ms ||
             !observed || observed.workspace_id!==proposal.workspace_id || typeof observed.path!=='string' || typeof observed.file_id!=='string' || !/^[a-f0-9]{64}$/.test(observed.content_sha256) || !Number.isSafeInteger(observed.size) || observed.size<0 || observed.size>1048576 ||
             !['before','after','different'].includes(inspection.match) || typeof inspection.same_file!=='boolean' || (!inspection.same_file && inspection.match!=='different') || !Number.isSafeInteger(inspection.observed_unix_ms) || inspection.observed_unix_ms<=0 || inspection.observed_unix_ms>8640000000000000 || inspection.quarantine_released!==false) throw new Error('The inspection does not match the selected uncertain edit. Refresh its current state.');
          post({type:'edit-inspection',id:proposal.id,inspection});
        } else if (['decide','review'].includes(message.type) && typeof message.id === 'string') {
          if (!vscode.workspace.isTrusted) throw new Error('Trust the workspace before deciding an operation.');
          if (message.type === 'decide' && message.decision !== 'allow' && message.decision !== 'deny') throw new Error('Decision must be allow or deny.');
          const proposal = reviewed.get(message.id);
          if (!proposal || proposal.run_id !== runId || proposal.state !== 'awaiting_approval') throw new Error('Inspect a pending operation in the selected run before deciding.');
          const version = generation;
          const current = await client.operation(message.id);
          if (panel !== view || version !== generation) return;
          if (current.id !== proposal.id || current.state !== 'awaiting_approval' || current.run_id !== proposal.run_id || current.workspace_id !== proposal.workspace_id || current.tool !== proposal.tool || current.arguments_json !== proposal.arguments_json || current.expires_unix_ms !== proposal.expires_unix_ms) {
            await poll();
            throw new Error('The operation changed since review. Inspect its current state before deciding.');
          }
          if (message.type === 'review') {await showEditReview(current);return;}
          const decided = await client.decide(message.id, message.decision);
          if (panel !== view || version !== generation) return;
          reviewed.set(decided.id, decided);
          post({ type: 'operations', operations: [...reviewed.values()] });
          await poll();
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
  context.subscriptions.push(vscode.commands.registerCommand('agentflow.configureToken', async initialToken => {
    try { await configureToken(initialToken); } catch (error) { vscode.window.showErrorMessage(error.message); }
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
  if(previewReady) {
    // View resolution waits for extension activation. Opening synchronously
    // inside activation would wait on itself in a normal development host.
    const bootstrap=setTimeout(()=>{
      open().then(async()=>{
        await vscode.commands.executeCommand('workbench.view.explorer');
        require('node:fs').writeFileSync(previewReady,JSON.stringify({opened:true,location:'secondarySidebar',origin:previewOrigin,time:new Date().toISOString(),storage:'Normal development host; no extension test runner',scope:'Actual sidebar opened; no live inference claimed'})+'\n');
      }).catch(error=>vscode.window.showErrorMessage(error.message));
    },0);
    context.subscriptions.push({dispose:()=>clearTimeout(bootstrap)});
  }
}

module.exports = { activate };
