// Real VS Code Extension Development Host bootstrap, without synthetic APIs.
// The CLI supplies its local access token privately in the process environment.
// Remain open for interactive inspection; no agent/model output is fabricated.
'use strict';
const fs=require('node:fs');
const vscode=require('vscode');
exports.run=async()=>{
  const token=process.env.XMIND_UI_BOOTSTRAP_TOKEN;
  const origin=process.env.XMIND_UI_BACKEND_ORIGIN;
  const ready=process.env.XMIND_UI_READY_FILE;
  if(!origin || !ready) throw new Error('UI bootstrap configuration is missing.');
  await vscode.workspace.getConfiguration('agentflow').update('backendUrl',origin,vscode.ConfigurationTarget.Global);
  await vscode.extensions.getExtension('agentflow-local.agentflow').activate();
  if(token) await vscode.commands.executeCommand('agentflow.configureToken',token);
  delete process.env.XMIND_UI_BOOTSTRAP_TOKEN;
  for(const group of vscode.window.tabGroups.all) for(const tab of group.tabs) {
    if(tab.label==='xMind' && tab.input instanceof vscode.TabInputWebview) await vscode.window.tabGroups.close(tab);
  }
  await vscode.commands.executeCommand('agentflow.open');
  fs.writeFileSync(ready,JSON.stringify({opened:true,location:'sidebar',origin,time:new Date().toISOString(),scope:'Actual VS Code sidebar provider opened; no live model configured or rendered screenshot verified'})+'\n');
  await new Promise(()=>{});
};
