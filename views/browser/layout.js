'use strict';
// Browser-only arrangement of the shared view. Existing controllers keep sole
// ownership of workspace/session selection and folder management.
(() => {
  const byId=id=>document.getElementById(id),make=(tag,text,cls)=>{const value=document.createElement(tag);if(text)value.textContent=text;if(cls)value.className=cls;return value;};
  const folderIcon=(size=22)=>{const svg=document.createElementNS('http://www.w3.org/2000/svg','svg'),path=document.createElementNS('http://www.w3.org/2000/svg','path');for(const [key,value] of Object.entries({viewBox:'0 0 24 24',width:String(size),height:String(size),fill:'none',stroke:'currentColor','stroke-width':'1.6','stroke-linejoin':'round','aria-hidden':'true'}))svg.setAttribute(key,value);path.setAttribute('d','M3 7a2 2 0 0 1 2-2h5l2 2h7a2 2 0 0 1 2 2v9a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2Z M3 10h18');svg.append(path);return svg;};
  const sidebarIcon=side=>{const ns='http://www.w3.org/2000/svg',svg=document.createElementNS(ns,'svg');for(const [key,value] of Object.entries({viewBox:'0 0 24 24',width:'18',height:'18',fill:'none',stroke:'currentColor','stroke-width':'1.7','aria-hidden':'true'}))svg.setAttribute(key,value);const frame=document.createElementNS(ns,'rect');for(const [key,value] of Object.entries({x:'3',y:'4',width:'18',height:'16',rx:'2'}))frame.setAttribute(key,value);const divider=document.createElementNS(ns,'path');divider.setAttribute('d',side==='left'?'M9 4v16':'M15 4v16');svg.append(frame,divider);return svg;};
  const layout=document.querySelector('.browser-layout'),chat=byId('agent-sidebar'),files=document.querySelector('.runtime-pane');
  if(!layout||!chat||!files)return;
  document.body.classList.add('conversation-layout');
  const folderDialog=byId('workspace-config-dialog'),folderTargets=make('div');folderTargets.hidden=true;
  folderDialog.querySelector(':scope > p:not([id])')?.remove();
  for(const element of [folderDialog.querySelector('label[for="workspace-picker"]'),byId('workspace-picker'),byId('workspace-switch-note'),byId('workspace-membership-note')])if(element)folderTargets.append(element);
  folderDialog.append(folderTargets);byId('workspace-config-title').textContent='Folders';folderDialog.querySelector('.workspace-config-folders h3')?.remove();
  const addFolder=byId('add-workspace-folder'),plus=document.createElementNS('http://www.w3.org/2000/svg','svg'),plusPath=document.createElementNS('http://www.w3.org/2000/svg','path');
  for(const [key,value] of Object.entries({viewBox:'0 0 24 24',width:'18',height:'18',fill:'none',stroke:'currentColor','stroke-width':'1.8','stroke-linecap':'round','aria-hidden':'true'}))plus.setAttribute(key,value);
  plusPath.setAttribute('d','M12 5v14M5 12h14');plus.append(plusPath);addFolder.replaceChildren(plus);addFolder.setAttribute('aria-label','Add folder');addFolder.title='Add folder';byId('workspace-config-close').before(addFolder);
  const folderList=byId('workspace-config-folders-list');
  const renderFolderRows=()=>{for(const row of folderList.children){if(row.querySelector('.folder-item-icon'))continue;const name=row.querySelector('strong'),path=row.querySelector('small');if(!name||!path)continue;
    const icon=make('span',undefined,'folder-item-icon');icon.append(folderIcon());
    const info=make('div',undefined,'folder-item-info');row.title=path.textContent;info.append(name,path);row.append(icon,info);
  }};
  new MutationObserver(renderFolderRows).observe(folderList,{childList:true,subtree:true});renderFolderRows();
  const settingsAdvanced=make('details',undefined,'settings-advanced');settingsAdvanced.append(make('summary','Advanced'));
  for(const id of ['connection-settings','mcp-settings','settings-info'])if(byId(id))settingsAdvanced.append(byId(id));byId('provider-settings').append(settingsAdvanced);
  byId('settings-info').textContent='API keys are stored securely on this computer.';
  const providerLabel=document.querySelector('label[for="provider-name"]');if(providerLabel)providerLabel.textContent='Provider';
  byId('provider-key-status').classList.add('redundant-setting-note');
  const providerHint=byId('provider-form').querySelector(':scope > p.hint:last-child');if(providerHint)providerHint.hidden=true;
  const gitDetails=document.querySelector('.workspace-git-details');
  if(gitDetails){const actions=make('div',undefined,'workspace-actions');for(const id of ['workspace-vcs-status','workspace-vcs-init'])if(byId(id))actions.append(byId(id));gitDetails.querySelector('summary').after(actions);}
  const status=byId('status'),compactStatus=()=>{status.hidden=['Ready','completed','queued','running','paused'].includes(status.textContent.trim());};
  new MutationObserver(compactStatus).observe(status,{childList:true,subtree:true,characterData:true});compactStatus();
  const navigation=make('aside',undefined,'workspace-navigation');navigation.id='workspace-navigation';navigation.setAttribute('aria-label','Workspaces and conversations');
  const heading=make('header'),title=make('strong','Workspaces'),add=make('button','＋');add.type='button';add.setAttribute('aria-label','Add workspace');add.title='Create workspace from a folder';add.onclick=()=>byId('add-workspace').click();heading.append(title,add);navigation.append(heading);
  const list=make('nav');list.id='workspace-conversations';list.setAttribute('aria-label','Workspace conversations');navigation.append(list);
  layout.prepend(navigation);
  const inspector=make('aside',undefined,'context-inspector');inspector.id='context-inspector';inspector.setAttribute('aria-label','Workspace details');
  const controls=make('header',undefined,'inspector-controls'),tabs=make('div');tabs.setAttribute('role','tablist');tabs.setAttribute('aria-label','Workspace detail views');
  const close=make('button','×');close.type='button';close.setAttribute('aria-label','Hide details');
  const panels=make('div',undefined,'inspector-panels'),activity=make('section');activity.id='inspector-activity';activity.setAttribute('role','tabpanel');activity.setAttribute('aria-labelledby','details-activity');
  files.id='inspector-files';files.setAttribute('role','tabpanel');files.setAttribute('aria-labelledby','details-files');
  const selection=make('details',undefined,'inspector-run');selection.append(make('summary','Run details'));for(const id of ['run-picker','run-context','context-view'])if(byId(id))selection.append(byId(id));activity.append(selection,byId('run-activity'));
  activity.prepend(document.querySelector('.conversation-tools'));
  const agentSettings=make('details',undefined,'agent-settings');agentSettings.append(make('summary','Agent settings'));for(const id of ['skills-view','agent-picker','workflow-picker'])if(byId(id))agentSettings.append(byId(id));activity.append(agentSettings);
  panels.append(files,activity);inspector.append(controls,panels);layout.append(inspector);
  const toggle=make('button',undefined,'sidebar-toggle');toggle.append(sidebarIcon('right'));toggle.id='details-toggle';toggle.type='button';toggle.setAttribute('aria-label','Toggle right sidebar');toggle.setAttribute('aria-controls',inspector.id);toggle.setAttribute('aria-expanded','true');toggle.title='Hide right sidebar';
  const leftToggle=make('button',undefined,'sidebar-toggle');leftToggle.append(sidebarIcon('left'));leftToggle.id='workspace-toggle';leftToggle.type='button';leftToggle.setAttribute('aria-label','Toggle left sidebar');leftToggle.setAttribute('aria-controls',navigation.id);leftToggle.setAttribute('aria-expanded','true');leftToggle.title='Hide left sidebar';
  const toolbar=document.querySelector('.browser-bar');toolbar.setAttribute('role','toolbar');toolbar.setAttribute('aria-label','Global toolbar');
  toolbar.prepend(leftToggle);
  if(byId('settings'))toolbar.append(byId('settings'));toolbar.append(toggle);
  toolbar.querySelector('.local-node')?.remove();
  // Connection lifecycle is automatic. Retain controller targets offscreen so
  // restoring a local session cannot reintroduce manual connection controls.
  const connectionTargets=make('div');connectionTargets.hidden=true;
  for(const id of ['connect-view','disconnect-view','refresh'])if(byId(id))connectionTargets.append(byId(id));toolbar.append(connectionTargets);
  // Workspace creation belongs to the sidebar +. The existing controller keeps
  // its command target, while workspace settings only manage existing folders.
  byId('add-workspace').hidden=true;document.querySelector('.conversation-header').hidden=true;
  const leftDivider=make('div',undefined,'pane-divider'),rightDivider=make('div',undefined,'pane-divider');leftDivider.id='workspace-divider';rightDivider.id='details-divider';
  navigation.after(leftDivider);inspector.before(rightDivider);
  leftToggle.onclick=()=>{navigation.hidden=!navigation.hidden;leftDivider.hidden=navigation.hidden;leftToggle.setAttribute('aria-expanded',String(!navigation.hidden));leftToggle.title=navigation.hidden?'Show left sidebar':'Hide left sidebar';};
  const resize=(divider,pane,variable,min,max,reversed=false)=>{
    const current=()=>Math.round(pane.getBoundingClientRect().width);
    const set=value=>{const other=reversed?navigation:inspector,available=layout.getBoundingClientRect().width-(other.hidden?0:other.getBoundingClientRect().width)-290;const limit=Math.max(min,Math.min(max,available));const width=Math.max(min,Math.min(limit,value));layout.style.setProperty(variable,width+'px');divider.setAttribute('aria-valuenow',String(Math.round(width)));};
    divider.tabIndex=0;divider.setAttribute('role','separator');divider.setAttribute('aria-orientation','vertical');divider.setAttribute('aria-label',reversed?'Resize details panel':'Resize workspace sidebar');divider.setAttribute('aria-valuemin',String(min));divider.setAttribute('aria-valuemax',String(max));divider.setAttribute('aria-valuenow',String(current()));
    let drag;
    divider.onpointerdown=event=>{if(event.button!==0)return;event.preventDefault();drag={x:event.clientX,width:current()};divider.setPointerCapture(event.pointerId);divider.classList.add('dragging');};
    divider.onpointermove=event=>{if(drag)set(drag.width+(event.clientX-drag.x)*(reversed?-1:1));};
    const end=()=>{drag=undefined;divider.classList.remove('dragging');};divider.onpointerup=end;divider.onpointercancel=end;divider.onlostpointercapture=end;
    divider.onkeydown=event=>{const step=event.shiftKey?32:12;if(event.key==='Home'){event.preventDefault();set(min);}else if(event.key==='End'){event.preventDefault();set(max);}else if(['ArrowLeft','ArrowRight'].includes(event.key)){event.preventDefault();set(current()+(event.key==='ArrowRight'?step:-step)*(reversed?-1:1));}};
  };
  resize(leftDivider,navigation,'--workspace-pane-width',140,360);resize(rightDivider,inspector,'--details-pane-width',240,640,true);
  let selected='files';
  const choose=name=>{selected=name;files.hidden=name!=='files';activity.hidden=name!=='activity';if(name==='activity')byId('run-activity').open=true;for(const button of tabs.children){button.setAttribute('aria-selected',String(button.dataset.panel===name));button.tabIndex=button.dataset.panel===name?0:-1;}inspector.hidden=false;rightDivider.hidden=false;toggle.setAttribute('aria-expanded','true');toggle.title='Hide right sidebar';};
  for(const [name,label] of [['files','Files'],['activity','Activity']]){const tab=make('button',label);tab.type='button';tab.id='details-'+name;tab.dataset.panel=name;tab.setAttribute('role','tab');tab.setAttribute('aria-controls',name==='files'?files.id:activity.id);tab.onclick=()=>choose(name);tab.onkeydown=event=>{if(['ArrowLeft','ArrowRight','Home','End'].includes(event.key)){event.preventDefault();const next=name==='files'?'activity':'files';choose(next);byId('details-'+next).focus();}};tabs.append(tab);}
  controls.append(tabs,close);close.onclick=()=>{inspector.hidden=true;rightDivider.hidden=true;toggle.setAttribute('aria-expanded','false');toggle.title='Show right sidebar';toggle.focus();};toggle.onclick=()=>{if(inspector.hidden)choose(selected);else close.click();};
  choose('files');
  // Approval review reveals its actual controls even when details were hidden.
  byId('approval-review').addEventListener('click',()=>choose('activity'));
  byId('failure-review').addEventListener('click',()=>choose('activity'));
  const originalSessions=byId('sessions');originalSessions.classList.add('navigation-source');
  const collapsedWorkspaces=new Set();
  const refresh=()=>{
    const picker=byId('workspace-picker'),active=picker.value;list.replaceChildren();add.disabled=byId('add-workspace').disabled;
    for(const option of picker.options){
      const name=option.dataset.workspaceName||option.textContent;
      const group=make('section',undefined,'workspace-group'),row=make('div',undefined,'workspace-row'),select=make('button',undefined,'workspace-toggle'),configure=make('button','⚙');
      const icon=folderIcon(18);icon.classList.add('workspace-folder-icon');const label=make('span',name,'workspace-name'),chevron=document.createElementNS('http://www.w3.org/2000/svg','svg'),arrow=document.createElementNS('http://www.w3.org/2000/svg','path');for(const [key,value] of Object.entries({viewBox:'0 0 24 24',width:'12',height:'12',fill:'none',stroke:'currentColor','stroke-width':'1.8','aria-hidden':'true'}))chevron.setAttribute(key,value);arrow.setAttribute('d','m9 5 7 7-7 7');chevron.append(arrow);chevron.classList.add('workspace-chevron');select.append(icon,label,chevron);
      const expanded=option.value===active&&!collapsedWorkspaces.has(option.value);
      select.type='button';select.dataset.workspaceId=option.value;select.title=option.title||option.textContent;select.setAttribute('aria-expanded',String(expanded));select.classList.toggle('selected',option.value===active);
      select.onclick=()=>{if(picker.value!==option.value){collapsedWorkspaces.delete(option.value);picker.value=option.value;picker.dispatchEvent(new Event('change',{bubbles:true}));}else{if(collapsedWorkspaces.has(option.value))collapsedWorkspaces.delete(option.value);else collapsedWorkspaces.add(option.value);refresh();[...list.querySelectorAll('.workspace-toggle')].find(button=>button.dataset.workspaceId===option.value)?.focus();}};
      configure.type='button';configure.setAttribute('aria-label','Configure '+option.textContent+' folders');configure.title='Manage workspace folders';configure.disabled=option.value!==active||byId('workspace-config-open').disabled;configure.onclick=()=>byId('workspace-config-open').click();row.append(select,configure);group.append(row);
      if(expanded){
        const sessions=make('div',undefined,'workspace-sessions');
        for(const session of originalSessions.options)if(session.dataset.sessionId){const button=make('button',session.textContent,'conversation-link');button.type='button';button.title=session.dataset.sessionTitle||session.textContent;button.classList.toggle('selected',session.value===originalSessions.value);if(session.value===originalSessions.value)button.setAttribute('aria-current','page');button.onclick=()=>{originalSessions.value=session.value;originalSessions.dispatchEvent(new Event('change',{bubbles:true}));};sessions.append(button);}
        const fresh=make('button','＋ New conversation','new-conversation');fresh.type='button';fresh.onclick=()=>byId('new').click();sessions.append(fresh);group.append(sessions);
      }
      list.append(group);
    }
  };
  const observer=new MutationObserver(refresh);for(const id of ['workspace-picker','sessions'])observer.observe(byId(id),{childList:true,subtree:true,attributes:true});
  for(const id of ['workspace-config-open','add-workspace'])observer.observe(byId(id),{attributes:true,attributeFilter:['disabled']});
  originalSessions.addEventListener('change',refresh);byId('workspace-picker').addEventListener('change',refresh);refresh();
  // A comparison or terminal request opens Files without submitting a command.
  for(const id of ['workspace-terminal-toggle','workspace-vcs-diff'])byId(id).addEventListener('click',()=>choose('files'));
})();
