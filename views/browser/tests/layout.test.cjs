'use strict';
const test=require('node:test'),assert=require('node:assert/strict'),fs=require('node:fs'),path=require('node:path');
const {JSDOM}=require('../../../extensions/vscode/node_modules/jsdom');
const {browserHtml}=require('../page.cjs');
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
  doc.querySelector('[aria-label="Hide details"]').click();assert.equal(doc.getElementById('context-inspector').hidden,true);doc.getElementById('details-toggle').click();assert.equal(doc.getElementById('context-inspector').hidden,false);
  doc.getElementById('details-activity').click();assert.equal(doc.getElementById('inspector-files').hidden,true);assert.equal(doc.getElementById('inspector-activity').hidden,false);assert.equal(doc.getElementById('run-activity').open,true);
  doc.getElementById('details-files').click();assert.equal(doc.getElementById('inspector-files').hidden,false);
 }finally{dom.window.close();}
});
