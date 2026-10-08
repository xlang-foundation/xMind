'use strict';
const {html}=require('../../extensions/vscode/webview.js');
// Pure production page assembly, shared by packaging and classic-script tests.
// It does not build assets, connect to a backend or acquire a credential.
function browserHtml(){
  const icon=(paths)=>`<svg viewBox="0 0 24 24" width="18" height="18" fill="none" stroke="currentColor" stroke-width="1.7" stroke-linecap="round" stroke-linejoin="round" aria-hidden="true">${paths}</svg>`;
  const connectIcon=icon('<path d="M8 3v5m8-5v5M6 8h12v3a6 6 0 0 1-12 0V8Zm6 9v4"/>');
  const disconnectIcon=icon('<path d="m3 3 18 18M8 3v3m8-3v5M6 8v3a6 6 0 0 0 6 6v4m6-13v3a6 6 0 0 1-1 3"/>');
  let page=html('browser-view',{source:"'self'",css:'/ui/chat.css',script:'/ui/chat.js',marked:'/ui/marked.js',purify:'/ui/purify.js'});
  page=page.replace(/<meta http-equiv="Content-Security-Policy"[^>]+>/,'').replace(/<script nonce="browser-view">[^<]*<\/script>/,'');
  page=page.replace('</head>','<title>xMind · Browser</title><link rel="stylesheet" href="/ui/browser.css"></head>').replace('<body>',`<body><nav class="browser-bar"><strong>xMind</strong><span>Shared agent runtime</span><button id="connect-view" class="connection-icon" aria-label="Connect to server" title="Connect to server">${connectIcon}</button><button id="disconnect-view" class="connection-icon" aria-label="Disconnect view" title="Disconnect view" hidden>${disconnectIcon}</button></nav><div class="browser-layout"><section class="runtime-pane"><h1>Agent workspace</h1><p id="workspace-info">Connect to your native xMind Server.</p><div id="browser-review" hidden><h2>Change comparison</h2><div class="review-columns"><section><h3>Before</h3><pre id="review-before"></pre></section><section><h3>After</h3><pre id="review-after"></pre></section></div></div></section><aside id="agent-sidebar" aria-label="xMind agent sidebar">`);
  page=page.replace('<dialog id="provider-settings"','</aside></div><dialog id="connection" aria-labelledby="connection-title"><form id="connection-form"><div class="settings-heading"><h2 id="connection-title">Connect to xMind Server</h2><button id="connection-close" type="button" aria-label="Close connection settings" title="Close">×</button></div><p>Enter this native server’s access token. Provider API keys belong in Settings after connecting.</p><label for="server-token">Server access token</label><input id="server-token" type="password" autocomplete="off" spellcheck="false" minlength="32" maxlength="256" required><p id="connection-error" role="status"></p><div class="connection-actions"><button id="connection-cancel" type="button">Cancel</button><button class="primary" type="submit">Connect</button></div></form></dialog><dialog id="provider-settings"');
  page=page.replace('<script nonce="browser-view" src="/ui/marked.js">','<script src="/ui/client.js"></script><script src="/ui/browser.js"></script><script nonce="browser-view" src="/ui/marked.js">');
  return page.replace('<aside id="agent-sidebar"','<div id="sidebar-divider" role="separator" aria-label="Resize agent sidebar" aria-orientation="vertical" tabindex="0"></div><aside id="agent-sidebar"');
}
module.exports={browserHtml};
