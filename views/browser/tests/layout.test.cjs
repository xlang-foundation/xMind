'use strict';
const test=require('node:test'),assert=require('node:assert/strict'),fs=require('node:fs'),path=require('node:path');
const {JSDOM}=require('../../../extensions/vscode/node_modules/jsdom');
const {browserHtml}=require('../page.cjs');
test('bounded composer menus forward selection, preserve it on Escape, and follow native metadata',async()=>{
 const dom=new JSDOM(browserHtml(),{runScripts:'outside-only'}),doc=dom.window.document,model=doc.getElementById('model'),changes=[];
 try{
  model.replaceChildren();for(const id of ['saved-model','a-very-long-model-name-that-must-not-stretch-the-menu']){const option=doc.createElement('option');option.value=id;option.textContent=id;model.append(option);}model.value='saved-model';model.disabled=false;model.addEventListener('change',()=>changes.push(model.value));
  dom.window.eval(fs.readFileSync(path.join(__dirname,'../layout.js'),'utf8'));
  const trigger=doc.getElementById('model-trigger'),popup=doc.getElementById('model-options');assert.equal(trigger.textContent,'saved-model');trigger.click();assert.equal(popup.hidden,false);assert.equal(popup.style.width,'260px');
  trigger.dispatchEvent(new dom.window.KeyboardEvent('keydown',{key:'ArrowDown',bubbles:true,cancelable:true}));assert.equal(doc.activeElement.dataset.value,'saved-model');popup.dispatchEvent(new dom.window.KeyboardEvent('keydown',{key:'Escape',bubbles:true,cancelable:true}));assert.equal(popup.hidden,true);assert.deepEqual(changes,[]);assert.equal(doc.activeElement,trigger);
  trigger.click();popup.querySelectorAll('button')[1].click();assert.deepEqual(changes,['a-very-long-model-name-that-must-not-stretch-the-menu']);assert.equal(popup.hidden,true);assert.equal(trigger.textContent,model.value);
  const same=doc.getElementById('model-trigger');model.disabled=true;await new Promise(resolve=>setImmediate(resolve));assert.equal(same.disabled,true);assert.equal(doc.getElementById('model-trigger'),same,'Native acknowledgements must not replace the control under the pointer');
 }finally{dom.window.close();}
});
test('unchanged background control updates retain rows and inline hover toolbars',async()=>{
 const dom=new JSDOM(browserHtml(),{runScripts:'outside-only'}),doc=dom.window.document;
 try{
  const workspace=doc.createElement('option');workspace.value='workspace';workspace.textContent='Workspace';doc.getElementById('workspace-picker').append(workspace);const session=doc.createElement('option');session.value='saved';session.textContent='Saved';session.dataset.sessionId='saved';doc.getElementById('sessions').append(session);doc.getElementById('workspace-config-open').disabled=false;doc.getElementById('rename').hidden=false;
  dom.window.eval(fs.readFileSync(path.join(__dirname,'../layout.js'),'utf8'));const row=doc.querySelector('.session-row'),toolbar=row.querySelector('.session-toolbar');assert.equal(toolbar.querySelectorAll('button').length,3);assert.equal(doc.getElementById('session-item-menu'),null);
  doc.getElementById('rename').disabled=doc.getElementById('rename').disabled;doc.getElementById('workspace-config-open').disabled=false;await new Promise(resolve=>setImmediate(resolve));assert.equal(doc.querySelector('.session-row'),row);assert.equal(doc.querySelector('.session-toolbar'),toolbar,'Background acknowledgements must not replace the toolbar under the pointer');
  doc.getElementById('workspace-control').dataset.switching='true';await new Promise(resolve=>setImmediate(resolve));assert.equal(doc.getElementById('current-workspace').disabled,true);assert.equal(doc.querySelector('.conversation-link').disabled,true);assert.equal(doc.getElementById('workspace-navigation').getAttribute('aria-busy'),'true');
  doc.getElementById('workspace-control').dataset.switching='false';await new Promise(resolve=>setImmediate(resolve));assert.equal(doc.getElementById('current-workspace').disabled,false);assert.equal(doc.querySelector('.conversation-link').disabled,false);
 }finally{dom.window.close();}
});
test('inline session toolbar targets inactive rows and offers archive and restore directly',async()=>{
 const dom=new JSDOM(browserHtml(),{runScripts:'outside-only',url:'http://localhost/'}),win=dom.window,doc=win.document,calls=[];
 win.HTMLDialogElement.prototype.showModal=function(){this.open=true;};win.HTMLDialogElement.prototype.close=function(){this.open=false;};win.xMindView={postMessage:message=>calls.push(JSON.parse(JSON.stringify(message)))};
 try{
  const workspace=doc.createElement('option');workspace.value='workspace';workspace.textContent='Workspace';doc.getElementById('workspace-picker').append(workspace);
  const source=doc.getElementById('sessions');for(const [id,title]of [['current','Open chat'],['other','Other chat']]){const option=doc.createElement('option');option.value=id;option.textContent=title;option.dataset.sessionId=id;option.dataset.sessionTitle=title;source.append(option);}source.value='current';source.dataset.archiveAvailable='true';source.dataset.archivedSessionIds='[]';doc.getElementById('rename').hidden=false;doc.getElementById('workspace-config-open').disabled=false;
  win.eval(fs.readFileSync(path.join(__dirname,'../layout.js'),'utf8'));
  const item=name=>doc.querySelector('[data-session-action-id="other"][data-action="'+name.toLowerCase()+'"]'),reply=action=>win.dispatchEvent(new win.MessageEvent('message',{source:win,origin:win.location.origin,data:{type:'sidebar-session-result',id:'other',action,success:true}}));
  assert.equal(doc.querySelectorAll('.session-toolbar').length,2);assert.equal(doc.querySelector('[role=menu]'),null);item('Rename').click();doc.getElementById('session-action-name').value='Renamed chat';doc.querySelector('#session-action-dialog form').dispatchEvent(new win.Event('submit',{cancelable:true}));assert.deepEqual(calls.at(-1),{type:'sidebar-session-action',id:'other',action:'rename',title:'Renamed chat',expected_title:'Other chat'});assert.equal(source.value,'current');reply('rename');
  item('Archive').click();assert.equal(calls.at(-1).action,'archive');source.dataset.archivedSessionIds='["other"]';reply('archive');await new Promise(resolve=>setImmediate(resolve));assert.equal(doc.querySelectorAll('.conversation-link').length,1);assert.equal(source.value,'current');
  doc.querySelector('.archived-sessions-toggle').click();assert.equal(doc.querySelector('.conversation-link').textContent,'Other chat');assert.ok(item('Restore'));item('Restore').click();assert.equal(calls.at(-1).action,'restore');source.dataset.archivedSessionIds='[]';reply('restore');await new Promise(resolve=>setImmediate(resolve));doc.querySelector('.archived-sessions-toggle').click();
  item('Delete').click();assert.equal(doc.getElementById('session-action-dialog').open,true);assert.equal(calls.at(-1).action,'restore','Delete must wait for confirmation');doc.querySelector('#session-action-dialog form').dispatchEvent(new win.Event('submit',{cancelable:true}));assert.equal(calls.at(-1).id,'other');assert.equal(calls.at(-1).action,'delete');assert.equal(source.value,'current');
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
  const option=doc.createElement('option');option.value='saved';option.textContent='Saved conversation';option.dataset.sessionId='saved';sessions.append(option);sessions.selectedIndex=-1;
  doc.getElementById('add-workspace').disabled=false;doc.getElementById('workspace-config-open').disabled=false;
  let folderIntent=0,sessionIntent=0;doc.getElementById('workspace-config-open').onclick=()=>folderIntent++;sessions.onchange=()=>sessionIntent++;
  dom.window.eval(fs.readFileSync(path.join(__dirname,'../layout.js'),'utf8'));
  assert.equal(doc.querySelector('.browser-layout').firstElementChild.className,'workspace-navigation');assert.equal(doc.getElementById('inspector-files').closest('#context-inspector')?.id,'context-inspector');assert.equal(doc.getElementById('agent-sidebar').previousElementSibling.id,'sidebar-divider');
  assert.equal(doc.querySelectorAll('.workspace-group').length,2);assert.equal(doc.querySelector('.workspace-row').children.length,2);assert.equal(doc.querySelector('.workspace-toolbar').querySelectorAll('button').length,2);assert.equal(doc.querySelector('.workspace-configure').querySelectorAll('svg').length,1);assert.equal(doc.querySelector('.workspace-group .conversation-link').textContent,'Saved conversation');doc.querySelector('.conversation-link').focus();doc.querySelector('.conversation-link').click();assert.equal(sessionIntent,1);assert.equal(doc.activeElement.className,'conversation-link selected');
  doc.querySelector('[aria-label="Configure C++ folders"]').click();assert.equal(folderIntent,1);
  assert.equal(doc.getElementById('current-workspace').textContent,'C++');doc.getElementById('current-workspace').click();assert.equal(folderIntent,2);
  assert.equal(doc.querySelector('.header-context').previousElementSibling.tagName,'STRONG');
  doc.querySelector('[aria-label="Hide details"]').click();assert.equal(doc.getElementById('context-inspector').hidden,true);doc.getElementById('details-toggle').click();assert.equal(doc.getElementById('context-inspector').hidden,false);
  doc.getElementById('details-activity').click();assert.equal(doc.getElementById('inspector-files').hidden,true);assert.equal(doc.getElementById('inspector-activity').hidden,false);assert.equal(doc.getElementById('run-activity').open,true);
  doc.getElementById('details-files').click();assert.equal(doc.getElementById('inspector-files').hidden,false);
 }finally{dom.window.close();}
});

test('inspector separates Changes, opens file previews with Back, and remembers the chosen tab',async()=>{
 const dom=new JSDOM(browserHtml(),{runScripts:'outside-only',url:'http://localhost/ui/'}),win=dom.window,doc=win.document;
 try{
  win.localStorage.setItem('xmind.browser.details.tab','changes');win.eval(fs.readFileSync(path.join(__dirname,'../layout.js'),'utf8'));
  assert.equal(doc.querySelector('[role=tab][aria-selected=true]').id,'details-changes');assert.ok(doc.querySelector('#inspector-changes #workspace-vcs-diff'));assert.equal(doc.querySelector('#inspector-files .file-tree-heading'),null);
  doc.getElementById('details-files').click();assert.equal(win.localStorage.getItem('xmind.browser.details.tab'),'files');win.dispatchEvent(new win.Event('workspace-file-preview'));assert.equal(doc.getElementById('workspace-canvas').dataset.preview,'true');doc.querySelector('[aria-label="Back to files"]').click();assert.equal(doc.getElementById('workspace-canvas').dataset.preview,'false');
  doc.getElementById('details-changes').click();let requests=0;const source=doc.getElementById('workspace-terminal-toggle');source.onclick=()=>requests++;source.disabled=false;await new Promise(resolve=>setImmediate(resolve));doc.getElementById('global-terminal-toggle').click();assert.equal(requests,1);assert.equal(doc.getElementById('inspector-changes').hidden,false);assert.equal(doc.getElementById('browser-terminal-panel').parentElement.id,'agent-sidebar');
  doc.getElementById('details-changes').dispatchEvent(new win.KeyboardEvent('keydown',{key:'ArrowRight',bubbles:true}));assert.equal(doc.activeElement.id,'details-activity');
 }finally{win.close();}
});

test('live Activity follows native tool receipts without changing tabs, reexecuting, or expanding verbose details',()=>{
 const dom=new JSDOM(browserHtml(),{runScripts:'outside-only',url:'http://localhost/ui/'}),win=dom.window,doc=win.document;
 try{
  win.eval(fs.readFileSync(path.join(__dirname,'../layout.js'),'utf8'));const send=data=>win.dispatchEvent(new win.MessageEvent('message',{source:win,origin:win.location.origin,data}));
  send({type:'runs',selected:'root',runs:[{id:'root',state:'running'}]});send({type:'event',event:{run_id:'root',seq:1,kind:'tool.started',data:{activity_id:'read',name:'read_file',arguments:{path:'src/main.cpp'}}}});
  const row=doc.querySelector('.activity-item');assert.match(doc.getElementById('activity-now').textContent,/Reading file · src\/main.cpp/);assert.equal(row.dataset.state,'running');assert.equal(row.open,false);assert.equal(doc.getElementById('inspector-files').hidden,false);assert.equal(doc.getElementById('details-activity').dataset.live,'true');
  const done={type:'event',event:{run_id:'root',seq:2,kind:'tool.completed',data:{activity_id:'read',name:'read_file',data:{content:'synthetic file fixture'}}}};send(done);send(done);assert.equal(doc.querySelectorAll('.activity-item').length,1);assert.equal(row.dataset.state,'completed');assert.equal(doc.getElementById('activity-finished').open,false);assert.match(row.textContent,/src\/main.cpp/);
  send({type:'event',event:{run_id:'root',seq:3,kind:'run.completed',data:{}}});assert.equal(doc.getElementById('activity-now').textContent,'Completed');assert.equal(doc.getElementById('details-activity').dataset.live,'false');
  send({type:'history',history:[]});assert.equal(doc.querySelectorAll('.activity-item').length,0);
 }finally{win.close();}
});

test('Activity scopes child calls, preserves interrupted outcomes, and bounds finished rows',()=>{
 const dom=new JSDOM(browserHtml(),{runScripts:'outside-only',url:'http://localhost/ui/'}),win=dom.window,doc=win.document;
 try{
  win.eval(fs.readFileSync(path.join(__dirname,'../layout.js'),'utf8'));const send=data=>win.dispatchEvent(new win.MessageEvent('message',{source:win,origin:win.location.origin,data})),event=(run_id,seq,kind,data={})=>send({type:run_id==='root'?'event':'owned-event',child_id:run_id==='root'?undefined:run_id,event:{run_id,seq,kind,data}});
  send({type:'runs',selected:'root',runs:[{id:'root',state:'running'}]});for(const id of ['child-a','child-b'])event(id,1,'tool.started',{call_id:'same',name:'read_file',arguments:{path:id}});event('child-a',2,'tool.completed',{call_id:'same',name:'read_file',data:'fixture'});event('root',1,'run.completed');assert.equal(doc.querySelectorAll('.activity-item[data-state=running]').length,1);event('child-b',2,'run.failed');assert.equal(doc.querySelectorAll('.activity-item[data-state=interrupted]').length,1);
  send({type:'reset-run'});for(let i=0;i<105;i++){event('root',i*2+10,'tool.started',{call_id:String(i),name:'read_file',arguments:{path:'fixture-'+i}});event('root',i*2+11,'tool.completed',{call_id:String(i),name:'read_file',data:'fixture'});}assert.equal(doc.querySelectorAll('.activity-item').length,100);assert.match(doc.getElementById('activity-finished').textContent,/Finished · 100/);
  send({type:'runs',selected:'different',runs:[{id:'different',state:'completed'}]});assert.equal(doc.querySelectorAll('.activity-item').length,0);
 }finally{win.close();}
});
