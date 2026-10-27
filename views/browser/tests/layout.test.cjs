'use strict';
const test=require('node:test'),assert=require('node:assert/strict'),fs=require('node:fs'),path=require('node:path');
const {JSDOM}=require('../../../extensions/vscode/node_modules/jsdom');
const {browserHtml}=require('../page.cjs');
test('unchanged background control updates retain sidebar rows and an open hover menu',async()=>{
 const dom=new JSDOM(browserHtml(),{runScripts:'outside-only'}),doc=dom.window.document;
 try{
  const workspace=doc.createElement('option');workspace.value='workspace';workspace.textContent='Workspace';doc.getElementById('workspace-picker').append(workspace);const session=doc.createElement('option');session.value='saved';session.textContent='Saved';session.dataset.sessionId='saved';doc.getElementById('sessions').append(session);doc.getElementById('workspace-config-open').disabled=false;doc.getElementById('rename').hidden=false;
  dom.window.eval(fs.readFileSync(path.join(__dirname,'../layout.js'),'utf8'));const row=doc.querySelector('.session-row');row.querySelector('.session-actions').click();assert.equal(doc.getElementById('session-item-menu').hidden,false);
  doc.getElementById('rename').disabled=doc.getElementById('rename').disabled;doc.getElementById('workspace-config-open').disabled=false;await new Promise(resolve=>setImmediate(resolve));assert.equal(doc.querySelector('.session-row'),row);assert.equal(doc.getElementById('session-item-menu').hidden,false,'Background acknowledgements must not dismiss a menu under the pointer');
  doc.getElementById('workspace-control').dataset.switching='true';await new Promise(resolve=>setImmediate(resolve));assert.equal(doc.getElementById('current-workspace').disabled,true);assert.equal(doc.querySelector('.conversation-link').disabled,true);assert.equal(doc.getElementById('workspace-navigation').getAttribute('aria-busy'),'true');
  doc.getElementById('workspace-control').dataset.switching='false';await new Promise(resolve=>setImmediate(resolve));assert.equal(doc.getElementById('current-workspace').disabled,false);assert.equal(doc.querySelector('.conversation-link').disabled,false);
 }finally{dom.window.close();}
});
test('session hover menu targets inactive rows and offers persistent archive and restore intents',async()=>{
 const dom=new JSDOM(browserHtml(),{runScripts:'outside-only',url:'http://localhost/'}),win=dom.window,doc=win.document,calls=[];
 win.HTMLDialogElement.prototype.showModal=function(){this.open=true;};win.HTMLDialogElement.prototype.close=function(){this.open=false;};win.xMindView={postMessage:message=>calls.push(JSON.parse(JSON.stringify(message)))};
 try{
  const workspace=doc.createElement('option');workspace.value='workspace';workspace.textContent='Workspace';doc.getElementById('workspace-picker').append(workspace);
  const source=doc.getElementById('sessions');for(const [id,title]of [['current','Open chat'],['other','Other chat']]){const option=doc.createElement('option');option.value=id;option.textContent=title;option.dataset.sessionId=id;option.dataset.sessionTitle=title;source.append(option);}source.value='current';source.dataset.archiveAvailable='true';source.dataset.archivedSessionIds='[]';doc.getElementById('rename').hidden=false;doc.getElementById('workspace-config-open').disabled=false;
  win.eval(fs.readFileSync(path.join(__dirname,'../layout.js'),'utf8'));
  const open=()=>doc.querySelector('[data-session-menu-id="other"]').click(),item=name=>[...doc.querySelectorAll('#session-item-menu button')].find(button=>button.textContent===name),reply=action=>win.dispatchEvent(new win.MessageEvent('message',{source:win,origin:win.location.origin,data:{type:'sidebar-session-result',id:'other',action,success:true}}));
  open();assert.deepEqual([...doc.querySelectorAll('#session-item-menu button')].map(button=>button.textContent),['Rename','Archive','Delete']);item('Rename').click();doc.getElementById('session-action-name').value='Renamed chat';doc.querySelector('#session-action-dialog form').dispatchEvent(new win.Event('submit',{cancelable:true}));assert.deepEqual(calls.at(-1),{type:'sidebar-session-action',id:'other',action:'rename',title:'Renamed chat',expected_title:'Other chat'});assert.equal(source.value,'current');reply('rename');
  open();item('Archive').click();assert.equal(calls.at(-1).action,'archive');source.dataset.archivedSessionIds='["other"]';reply('archive');await new Promise(resolve=>setImmediate(resolve));assert.equal(doc.querySelectorAll('.conversation-link').length,1);assert.equal(source.value,'current');
  doc.querySelector('.archived-sessions-toggle').click();assert.equal(doc.querySelector('.conversation-link').textContent,'Other chat');open();assert.ok(item('Restore'));item('Restore').click();assert.equal(calls.at(-1).action,'restore');source.dataset.archivedSessionIds='[]';reply('restore');await new Promise(resolve=>setImmediate(resolve));doc.querySelector('.archived-sessions-toggle').click();
  open();item('Delete').click();assert.equal(doc.getElementById('session-action-dialog').open,true);assert.equal(calls.at(-1).action,'restore','Delete must wait for confirmation');doc.querySelector('#session-action-dialog form').dispatchEvent(new win.Event('submit',{cancelable:true}));assert.equal(calls.at(-1).id,'other');assert.equal(calls.at(-1).action,'delete');assert.equal(source.value,'current');
 }finally{win.close();}
});
test('header context follows native names and reuses workspace and session rename controls',async()=>{
 const dom=new JSDOM(browserHtml(),{runScripts:'outside-only'}),doc=dom.window.document;
 try{
  const workspace=doc.createElement('option');workspace.value='workspace';workspace.textContent='Display name · 2 folders · D:/first';workspace.dataset.workspaceName='Display name';doc.getElementById('workspace-picker').append(workspace);
  const session=doc.createElement('option');session.value='session';session.dataset.sessionId='session';session.dataset.sessionTitle='First title';session.textContent='First title · Running';doc.getElementById('sessions').append(session);
  let renamed=0;const rename=doc.getElementById('rename');rename.hidden=false;rename.disabled=false;rename.onclick=()=>renamed++;
  dom.window.eval(fs.readFileSync(path.join(__dirname,'../layout.js'),'utf8'));
  assert.equal(doc.getElementById('current-workspace').textContent,'Display name');assert.equal(doc.querySelector('.header-session-label').textContent,'First title');
  doc.getElementById('current-session').click();assert.equal(renamed,1);
  workspace.dataset.workspaceName='Renamed workspace';session.dataset.sessionTitle='A much longer manually chosen session title';await new Promise(resolve=>setImmediate(resolve));
  assert.equal(doc.getElementById('current-workspace').textContent,'Renamed workspace');assert.equal(doc.getElementById('current-session').title,'A much longer manually chosen session title');
  rename.disabled=true;await new Promise(resolve=>setImmediate(resolve));assert.equal(doc.getElementById('current-session').disabled,true);
 }finally{dom.window.close();}
});
test('browser layout keeps workspace conversations on the left and offers hideable Files and Activity tabs',async()=>{
 const dom=new JSDOM(browserHtml(),{runScripts:'outside-only'}),doc=dom.window.document;
 try{
  const picker=doc.getElementById('workspace-picker'),sessions=doc.getElementById('sessions');picker.disabled=false;
  for(const [id,title] of [['a','C++'],['b','Vue']]){const option=doc.createElement('option');option.value=id;option.textContent=title;picker.append(option);}picker.value='a';
  const option=doc.createElement('option');option.value='saved';option.textContent='Saved conversation';option.dataset.sessionId='saved';sessions.append(option);
  doc.getElementById('add-workspace').disabled=false;doc.getElementById('workspace-config-open').disabled=false;
  let folderIntent=0,sessionIntent=0;doc.getElementById('workspace-config-open').onclick=()=>folderIntent++;sessions.onchange=()=>sessionIntent++;
  dom.window.eval(fs.readFileSync(path.join(__dirname,'../layout.js'),'utf8'));
  assert.equal(doc.querySelector('.browser-layout').firstElementChild.className,'workspace-navigation');assert.equal(doc.getElementById('inspector-files').closest('#context-inspector')?.id,'context-inspector');assert.equal(doc.getElementById('agent-sidebar').previousElementSibling.id,'sidebar-divider');
  assert.equal(doc.querySelectorAll('.workspace-group').length,2);assert.equal(doc.querySelector('.workspace-group .conversation-link').textContent,'Saved conversation');doc.querySelector('.conversation-link').click();assert.equal(sessionIntent,1);
  doc.querySelector('[aria-label="Configure C++ folders"]').click();assert.equal(folderIntent,1);
  assert.equal(doc.getElementById('current-workspace').textContent,'C++');doc.getElementById('current-workspace').click();assert.equal(folderIntent,2);
  assert.equal(doc.querySelector('.header-context').previousElementSibling.tagName,'STRONG');
  doc.querySelector('[aria-label="Hide details"]').click();assert.equal(doc.getElementById('context-inspector').hidden,true);doc.getElementById('details-toggle').click();assert.equal(doc.getElementById('context-inspector').hidden,false);
  doc.getElementById('details-activity').click();assert.equal(doc.getElementById('inspector-files').hidden,true);assert.equal(doc.getElementById('inspector-activity').hidden,false);assert.equal(doc.getElementById('run-activity').open,true);
  doc.getElementById('details-files').click();assert.equal(doc.getElementById('inspector-files').hidden,false);
 }finally{dom.window.close();}
});
