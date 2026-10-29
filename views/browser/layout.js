'use strict';
// Browser-only arrangement of the shared view. Existing controllers keep sole
// ownership of workspace/session selection and folder management.
(() => {
  const byId=id=>document.getElementById(id),make=(tag,text,cls)=>{const value=document.createElement(tag);if(text)value.textContent=text;if(cls)value.className=cls;return value;};
  const preference=(key,fallback)=>{try{return localStorage.getItem('xmind.browser.'+key)||fallback;}catch{return fallback;}};
  const savePreference=(key,value)=>{try{localStorage.setItem('xmind.browser.'+key,String(value));}catch{}};
  const folderIcon=(size=22)=>{const svg=document.createElementNS('http://www.w3.org/2000/svg','svg'),path=document.createElementNS('http://www.w3.org/2000/svg','path');for(const [key,value] of Object.entries({viewBox:'0 0 24 24',width:String(size),height:String(size),fill:'none',stroke:'currentColor','stroke-width':'1.6','stroke-linejoin':'round','aria-hidden':'true'}))svg.setAttribute(key,value);path.setAttribute('d','M3 7a2 2 0 0 1 2-2h5l2 2h7a2 2 0 0 1 2 2v9a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2Z M3 10h18');svg.append(path);return svg;};
  const actionIcon=name=>{const ns='http://www.w3.org/2000/svg',svg=document.createElementNS(ns,'svg');for(const [key,value]of Object.entries({viewBox:'0 0 24 24',width:'16',height:'16',fill:'none',stroke:'currentColor','stroke-width':'1.7','stroke-linecap':'round','stroke-linejoin':'round','aria-hidden':'true'}))svg.setAttribute(key,value);const path=document.createElementNS(ns,'path');path.setAttribute('d',({rename:'m16 3 5 5-12 12H4v-5Z M14 5l5 5',archive:'M4 4h16v4H4z M6 8v12h12V8 M10 12h4',restore:'M4 8h5M4 8V3 M4 8a8 8 0 1 1-1 8',delete:'M3 6h18M9 6V3h6v3M5 6l1 15h12l1-15M10 10v7M14 10v7',settings:'M12 2v3m0 14v3M2 12h3m14 0h3M5 5l2 2m10 10 2 2M5 19l2-2M17 7l2-2'})[name]);svg.append(path);if(name==='settings')for(const radius of [6,2.5]){const circle=document.createElementNS(ns,'circle');circle.setAttribute('cx','12');circle.setAttribute('cy','12');circle.setAttribute('r',String(radius));svg.append(circle);}return svg;};
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
  const changes=make('section');changes.id='inspector-changes';changes.setAttribute('role','tabpanel');changes.setAttribute('aria-labelledby','details-changes');
  if(gitDetails){gitDetails.open=true;changes.append(gitDetails);}
  changes.prepend(byId('workspace-vcs-state'));changes.append(byId('browser-review'));
  files.querySelector('.workspace-heading').hidden=true;files.querySelector('.file-tree-heading').remove();
  const canvas=byId('workspace-canvas'),filePreview=files.querySelector('.file-preview'),back=make('button','←');back.type='button';back.setAttribute('aria-label','Back to files');back.title='Back to files';
  back.onclick=()=>{canvas.dataset.preview='false';byId('workspace-find-query').focus();};filePreview.querySelector('header').prepend(back);
  window.addEventListener('workspace-file-preview',()=>{canvas.dataset.preview='true';});
  new MutationObserver(()=>{if(canvas.hidden)canvas.dataset.preview='false';}).observe(canvas,{attributes:true,attributeFilter:['hidden']});
  const filesEmpty=make('p','Workspace files will appear when the workspace is ready.','inspector-empty');files.append(filesEmpty);
  const syncFilesEmpty=()=>filesEmpty.hidden=!canvas.hidden;new MutationObserver(syncFilesEmpty).observe(canvas,{attributes:true,attributeFilter:['hidden']});syncFilesEmpty();
  selection.prepend(activity.querySelector('.conversation-tools'));activity.append(selection,agentSettings);
  activity.querySelector('#events').parentElement.querySelector('summary').textContent='Diagnostic log';
  const terminalPanel=byId('browser-terminal-panel');inspector.append(terminalPanel);
  panels.append(files,changes,activity);inspector.prepend(controls,panels);layout.append(inspector);
  const toggle=make('button',undefined,'sidebar-toggle');toggle.append(sidebarIcon('right'));toggle.id='details-toggle';toggle.type='button';toggle.setAttribute('aria-label','Toggle right sidebar');toggle.setAttribute('aria-controls',inspector.id);toggle.setAttribute('aria-expanded','true');toggle.title='Hide right sidebar';
  const leftToggle=make('button',undefined,'sidebar-toggle');leftToggle.append(sidebarIcon('left'));leftToggle.id='workspace-toggle';leftToggle.type='button';leftToggle.setAttribute('aria-label','Toggle left sidebar');leftToggle.setAttribute('aria-controls',navigation.id);leftToggle.setAttribute('aria-expanded','true');leftToggle.title='Hide left sidebar';
  const toolbar=document.querySelector('.browser-bar');toolbar.setAttribute('role','toolbar');toolbar.setAttribute('aria-label','Global toolbar');
  toolbar.prepend(leftToggle);
  const context=make('div',undefined,'header-context'),workspaceContext=make('button',undefined,'header-workspace'),separator=make('span','/','header-context-separator'),sessionContext=make('button',undefined,'header-session'),sessionLabel=make('span',undefined,'header-session-label');
  workspaceContext.id='current-workspace';workspaceContext.type='button';workspaceContext.setAttribute('aria-label','Workspace settings');workspaceContext.onclick=()=>byId('workspace-config-open').click();
  sessionContext.id='current-session';sessionContext.type='button';sessionContext.setAttribute('aria-label','Rename current session');sessionContext.onclick=()=>byId('rename').click();
  const renameIcon=byId('rename').querySelector('svg').cloneNode(true);sessionContext.append(sessionLabel,renameIcon);context.append(workspaceContext,separator,sessionContext);toolbar.querySelector('strong').after(context);
  if(byId('settings'))toolbar.append(byId('settings'));toolbar.append(toggle);
  const terminalButton=make('button',undefined,'sidebar-toggle'),terminalSource=byId('workspace-terminal-toggle');terminalButton.id='global-terminal-toggle';terminalButton.type='button';terminalButton.setAttribute('aria-label','Toggle terminal');terminalButton.title='Terminal';
  terminalButton.innerHTML='<svg viewBox="0 0 24 24" width="18" height="18" fill="none" stroke="currentColor" stroke-width="1.7" stroke-linecap="round" stroke-linejoin="round" aria-hidden="true"><path d="m4 5 7 7-7 7m10 0h7"/></svg>';
  terminalButton.onclick=()=>terminalSource.click();const syncTerminal=()=>{terminalButton.disabled=terminalSource.disabled;terminalButton.setAttribute('aria-pressed',String(terminalSource.getAttribute('aria-pressed')==='true'));};new MutationObserver(syncTerminal).observe(terminalSource,{attributes:true,attributeFilter:['disabled','aria-pressed']});syncTerminal();
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
    const end=()=>{if(drag&&reversed)savePreference('details.width',current());drag=undefined;divider.classList.remove('dragging');};divider.onpointerup=end;divider.onpointercancel=end;divider.onlostpointercapture=end;
    divider.onkeydown=event=>{const step=event.shiftKey?32:12;if(event.key==='Home'){event.preventDefault();set(min);}else if(event.key==='End'){event.preventDefault();set(max);}else if(['ArrowLeft','ArrowRight'].includes(event.key)){event.preventDefault();set(current()+(event.key==='ArrowRight'?step:-step)*(reversed?-1:1));}else return;if(reversed)savePreference('details.width',current());};
    if(reversed){const saved=Number(preference('details.width',''));if(saved>=min&&saved<=max)set(saved);}
  };
  resize(leftDivider,navigation,'--workspace-pane-width',140,360);resize(rightDivider,inspector,'--details-pane-width',240,640,true);
  let selected='files';
  const panelNames=['files','changes','activity'];
  const choose=name=>{selected=panelNames.includes(name)?name:'files';for(const panel of [files,changes,activity])panel.hidden=panel.id!=='inspector-'+selected;if(selected==='activity')byId('run-activity').open=true;for(const button of tabs.children){button.setAttribute('aria-selected',String(button.dataset.panel===selected));button.tabIndex=button.dataset.panel===selected?0:-1;}inspector.hidden=false;rightDivider.hidden=false;toggle.setAttribute('aria-expanded','true');toggle.title='Hide right sidebar';savePreference('details.tab',selected);};
  for(const [name,label] of [['files','Files'],['changes','Changes'],['activity','Activity']]){const tab=make('button',label);tab.type='button';tab.id='details-'+name;tab.dataset.panel=name;tab.setAttribute('role','tab');tab.setAttribute('aria-controls','inspector-'+name);tab.onclick=()=>choose(name);tab.onkeydown=event=>{if(['ArrowLeft','ArrowRight','Home','End'].includes(event.key)){event.preventDefault();const index=panelNames.indexOf(name),next=event.key==='Home'?'files':event.key==='End'?'activity':panelNames[(index+(event.key==='ArrowRight'?1:2))%3];choose(next);byId('details-'+next).focus();}};tabs.append(tab);}
  controls.append(tabs,terminalButton,close);close.onclick=()=>{inspector.hidden=true;rightDivider.hidden=true;toggle.setAttribute('aria-expanded','false');toggle.title='Show right sidebar';toggle.focus();};toggle.onclick=()=>{if(inspector.hidden)choose(selected);else close.click();};
  choose(preference('details.tab','files'));
  // Approval review reveals its actual controls even when details were hidden.
  byId('approval-review').addEventListener('click',()=>choose('activity'));
  byId('failure-review').addEventListener('click',()=>choose('activity'));
  document.addEventListener('keydown',event=>{if(['Tab','ArrowLeft','ArrowRight','ArrowUp','ArrowDown','Home','End'].includes(event.key))navigation.classList.add('keyboard-navigation');},{capture:true});
  document.addEventListener('pointerdown',()=>navigation.classList.remove('keyboard-navigation'),{capture:true});
  const originalSessions=byId('sessions');originalSessions.classList.add('navigation-source');
  let showArchived=false,archiveWorkspace,pendingAction;
  const archiveToggle=make('button','Archived sessions','archived-sessions-toggle');archiveToggle.type='button';archiveToggle.hidden=true;archiveToggle.onclick=()=>{showArchived=!showArchived;refresh();};navigation.append(archiveToggle);
  const sessionNotice=make('p',undefined,'session-action-notice');sessionNotice.hidden=true;sessionNotice.setAttribute('role','status');navigation.append(sessionNotice);
  const actionDialog=make('dialog',undefined,'session-action-dialog'),actionForm=make('form'),actionHeading=make('h2'),actionClose=make('button','×','session-action-close'),actionLabel=make('label','Name'),actionInput=make('input'),actionDescription=make('p'),actionError=make('p',undefined,'session-action-error'),actionButtons=make('div',undefined,'session-action-buttons'),actionCancel=make('button','Cancel'),actionSave=make('button','Save');
  actionDialog.id='session-action-dialog';actionHeading.id='session-action-heading';actionDialog.setAttribute('aria-labelledby',actionHeading.id);actionInput.id='session-action-name';actionInput.maxLength=256;actionInput.autocomplete='off';actionLabel.htmlFor=actionInput.id;actionClose.type='button';actionClose.setAttribute('aria-label','Close session action');actionCancel.type='button';actionSave.type='submit';actionError.setAttribute('role','alert');actionButtons.append(actionCancel,actionSave);actionForm.append(actionHeading,actionClose,actionLabel,actionInput,actionDescription,actionError,actionButtons);actionDialog.append(actionForm);document.body.append(actionDialog);
  const closeActionDialog=()=>{if(!pendingAction){actionDialog.close();actionInput.value='';[...list.querySelectorAll('.session-row')].find(row=>row.dataset.sessionId===actionDialog.dataset.sessionId)?.querySelector('.conversation-link')?.focus();}};actionClose.onclick=closeActionDialog;actionCancel.onclick=closeActionDialog;actionDialog.oncancel=event=>{event.preventDefault();closeActionDialog();};
  const sendSessionAction=(target,action,title)=>{pendingAction={...target,action};actionSave.disabled=true;actionCancel.disabled=true;actionClose.disabled=true;actionInput.disabled=true;sessionNotice.hidden=true;window.xMindView.postMessage({type:'sidebar-session-action',id:target.id,action,...(action==='rename'?{title,expected_title:target.title}:{})});};
  const chooseSessionAction=(target,action)=>{
    if(!target)return;
    if(action==='archive'||action==='restore'){sendSessionAction(target,action);return;}
    actionDialog.dataset.action=action;actionDialog.dataset.sessionId=target.id;actionDialog.dataset.expectedTitle=target.title;
    actionHeading.textContent=action==='rename'?'Rename session':'Delete session?';actionLabel.hidden=actionInput.hidden=action!=='rename';actionDescription.hidden=action==='rename';actionDescription.textContent='Delete “'+target.title+'”? You won’t be able to restore it from the app.';actionInput.value=target.title;actionInput.disabled=false;actionError.hidden=true;actionError.textContent='';actionSave.textContent=action==='rename'?'Save':'Delete';actionSave.classList.toggle('danger',action==='delete');actionSave.disabled=actionCancel.disabled=actionClose.disabled=false;actionDialog.showModal();(action==='rename'?actionInput:actionCancel).focus();
  };
  actionForm.onsubmit=event=>{event.preventDefault();if(pendingAction)return;const action=actionDialog.dataset.action,title=actionInput.value.trim();if(action==='rename'&&!title){actionError.hidden=false;actionError.textContent='Enter a session name.';return;}sendSessionAction({id:actionDialog.dataset.sessionId,title:actionDialog.dataset.expectedTitle},action,title);};
  window.addEventListener('message',event=>{
    if(event.source!==window||event.origin!==location.origin)return;const result=event.data;if(!pendingAction||!['sidebar-session-result','session-delete-result'].includes(result?.type)||result.id!==pendingAction.id)return;
    const operation=pendingAction;pendingAction=undefined;actionSave.disabled=actionCancel.disabled=actionClose.disabled=actionInput.disabled=false;refresh();
    if(result.success){actionDialog.close();sessionNotice.hidden=false;sessionNotice.textContent=operation.action==='archive'?'Session archived. Open Archived sessions to restore it.':operation.action==='restore'?'Session restored.':operation.action==='delete'?'Session deleted.':'Session renamed.';}
    else if(actionDialog.open){actionError.hidden=false;actionError.textContent=result.text||'The session could not be updated.';}
    else{sessionNotice.hidden=false;sessionNotice.textContent=result.text||'The session could not be updated.';}
    if(result.success)queueMicrotask(()=>{const target=[...list.querySelectorAll('.session-row')].find(row=>row.dataset.sessionId===operation.id)?.querySelector('.conversation-link')||(!archiveToggle.hidden?archiveToggle:list.querySelector('.conversation-link'));target?.focus();});
  });
  const sessionCatalogues=new Map();let sourceWorkspaceId=byId('workspace-picker').value||undefined,navigationSignature,defaultExpanded=false;
  let savedExpanded;try{savedExpanded=JSON.parse(preference('workspace.expanded','null'));}catch{}
  const expandedWorkspaces=new Set(Array.isArray(savedExpanded)?savedExpanded.filter(id=>typeof id==='string').slice(0,256):[]);
  const saveExpanded=()=>savePreference('workspace.expanded',JSON.stringify([...expandedWorkspaces]));
  const requestSessions=(id,force=false)=>{
    const existing=sessionCatalogues.get(id);if(existing?.loading||existing&&!force)return;
    sessionCatalogues.set(id,{options:[],archived:new Set(),...existing,loading:true,error:undefined});
    queueMicrotask(()=>window.xMindView?.postMessage({type:'workspace-session-list',workspace_id:id}));
  };
  window.addEventListener('message',event=>{
    if(event.source!==window||event.origin!==location.origin)return;const message=event.data;
    if(message?.type==='sessions'){sourceWorkspaceId=message.workspace_id;queueMicrotask(refresh);return;}
    if(message?.type!=='workspace-session-catalogue'||![...byId('workspace-picker').options].some(option=>option.value===message.workspace_id))return;
    const old=sessionCatalogues.get(message.workspace_id);
    if(message.error)sessionCatalogues.set(message.workspace_id,{options:[],archived:new Set(),...old,loading:false,error:message.error});
    else sessionCatalogues.set(message.workspace_id,{options:message.sessions.map(item=>({value:item.id,textContent:item.title,dataset:{sessionId:item.id,sessionTitle:item.title,sessionActive:String(item.active)}})),archived:new Set(message.sessions.filter(item=>item.archived).map(item=>item.id)),archiveAvailable:message.archive_capability===true,loading:false});
    refresh();
  });
  const refresh=()=>{
    const picker=byId('workspace-picker'),active=picker.value;if(archiveWorkspace!==active){showArchived=false;archiveWorkspace=active;sessionNotice.hidden=true;}
    const switching=byId('workspace-control').dataset.switching==='true';navigation.setAttribute('aria-busy',String(switching));
    if(!defaultExpanded&&active){defaultExpanded=true;if(!Array.isArray(savedExpanded))expandedWorkspaces.add(active);}
    if(sourceWorkspaceId===active&&!switching)sessionCatalogues.set(active,{options:[...originalSessions.options].filter(option=>option.dataset.sessionId).map(option=>({value:option.value,textContent:option.textContent,dataset:{...option.dataset}})),archived:new Set(JSON.parse(originalSessions.dataset.archivedSessionIds||'[]')),archiveAvailable:originalSessions.dataset.archiveAvailable==='true',loading:false});
    for(const id of expandedWorkspaces)if([...picker.options].some(option=>option.value===id)&&!sessionCatalogues.has(id))requestSessions(id);
    const signature=JSON.stringify([active,switching,showArchived,[...expandedWorkspaces],[...sessionCatalogues].map(([id,value])=>[id,value.options,[...value.archived],value.loading,value.error]),!!pendingAction,byId('add-workspace').disabled,byId('workspace-config-open').disabled,byId('rename').disabled,byId('rename').hidden,originalSessions.value,originalSessions.dataset.archivedSessionIds,originalSessions.dataset.archiveAvailable,[...picker.options].map(option=>[option.value,option.textContent,option.title,option.dataset.workspaceName])]);
    if(signature===navigationSignature)return;navigationSignature=signature;
    const focused=document.activeElement,focusedSession=focused.closest('.session-row')?.dataset.sessionId,focusedSessionWorkspace=focused.closest('.session-row')?.dataset.workspaceId,focusedWorkspace=focused.closest('.workspace-row')?.querySelector('.workspace-toggle')?.dataset.workspaceId,focusedAction=focused.dataset.action,workspaceAction=focused.classList.contains('workspace-new-session')?'new':focused.classList.contains('workspace-configure')?'configure':focused.classList.contains('workspace-disclosure')?'expand':'select';
    list.replaceChildren();add.disabled=byId('add-workspace').disabled;
    const archived=new Set(JSON.parse(originalSessions.dataset.archivedSessionIds||'[]')),archiveCount=[...originalSessions.options].filter(item=>archived.has(item.value)).length;archiveToggle.hidden=originalSessions.dataset.archiveAvailable!=='true'||(!showArchived&&archiveCount===0);archiveToggle.textContent='Archived sessions ('+archiveCount+')';archiveToggle.setAttribute('aria-pressed',String(showArchived));
    const workspace=picker.selectedOptions[0],session=sourceWorkspaceId===active?originalSessions.selectedOptions[0]:undefined;
    workspaceContext.textContent=workspace?.dataset.workspaceName||workspace?.textContent||'Workspace';workspaceContext.title=workspaceContext.textContent;workspaceContext.disabled=switching||byId('workspace-config-open').disabled;
    sessionLabel.textContent=session?.dataset.sessionTitle||session?.textContent||'New session';sessionContext.title=sessionLabel.textContent;sessionContext.disabled=switching||byId('rename').disabled||byId('rename').hidden;
    for(const option of picker.options){
      const name=option.dataset.workspaceName||option.textContent;
      const group=make('section',undefined,'workspace-group'),row=make('div',undefined,'workspace-row'),select=make('button',undefined,'workspace-toggle'),configure=make('button',undefined,'workspace-configure');configure.append(actionIcon('settings'));
      const icon=folderIcon(18);icon.classList.add('workspace-folder-icon');const label=make('span',name,'workspace-name'),chevron=document.createElementNS('http://www.w3.org/2000/svg','svg'),arrow=document.createElementNS('http://www.w3.org/2000/svg','path');for(const [key,value] of Object.entries({viewBox:'0 0 24 24',width:'12',height:'12',fill:'none',stroke:'currentColor','stroke-width':'1.8','aria-hidden':'true'}))chevron.setAttribute(key,value);arrow.setAttribute('d','m9 5 7 7-7 7');chevron.append(arrow);chevron.classList.add('workspace-chevron');select.append(icon,label,chevron);
      const expanded=expandedWorkspaces.has(option.value),title=make('div',undefined,'workspace-title'),disclosure=make('button',undefined,'workspace-disclosure');
      select.removeChild(chevron);select.type='button';select.disabled=switching&&option.value!==active;select.setAttribute('aria-busy',String(switching&&option.value===active));select.dataset.workspaceId=option.value;select.title=option.title||option.textContent;select.classList.toggle('selected',option.value===active);title.classList.toggle('selected',option.value===active);
      select.onclick=()=>{expandedWorkspaces.add(option.value);saveExpanded();if(picker.value!==option.value){picker.value=option.value;picker.dispatchEvent(new Event('change',{bubbles:true}));}else refresh();};
      disclosure.type='button';disclosure.append(chevron);disclosure.setAttribute('aria-label','Toggle sessions in '+name);disclosure.setAttribute('aria-expanded',String(expanded));disclosure.title=expanded?'Collapse sessions':'Expand sessions';disclosure.onclick=()=>{if(expandedWorkspaces.has(option.value))expandedWorkspaces.delete(option.value);else{expandedWorkspaces.add(option.value);if(option.value!==active)requestSessions(option.value,true);}saveExpanded();refresh();[...list.querySelectorAll('.workspace-toggle')].find(button=>button.dataset.workspaceId===option.value)?.parentElement.querySelector('.workspace-disclosure')?.focus();};title.append(select,disclosure);
      const fresh=make('button',undefined,'workspace-new-session');fresh.type='button';fresh.append(plus.cloneNode(true));fresh.setAttribute('aria-label','New session in '+name);fresh.title='New session';fresh.disabled=switching||option.value!==active;fresh.onclick=()=>{showArchived=false;byId('new').click();};
      configure.type='button';configure.setAttribute('aria-label','Configure '+option.textContent+' folders');configure.title='Manage workspace folders';configure.disabled=switching||option.value!==active||byId('workspace-config-open').disabled;configure.onclick=()=>byId('workspace-config-open').click();const toolbar=make('div',undefined,'workspace-toolbar hover-toolbar');toolbar.setAttribute('role','toolbar');toolbar.setAttribute('aria-label','Workspace actions for '+name);toolbar.append(fresh,configure);row.append(title,toolbar);group.append(row);
      if(expanded){
        const sessions=make('div',undefined,'workspace-sessions');
        // The native selection changes before its session catalogue arrives.
        // Keep local disclosure available without placing the previous
        // workspace's conversations under the newly selected workspace.
        const catalogue=sessionCatalogues.get(option.value),hiddenForSwitch=switching&&option.value===active,workspaceArchived=catalogue?.archived||new Set(),showWorkspaceArchived=option.value===active&&showArchived;
        if(hiddenForSwitch||catalogue?.loading)sessions.append(make('p','Loading sessions…','empty-session-list'));
        if(catalogue?.error)sessions.append(make('p',catalogue.error,'empty-session-list'));
        for(const session of hiddenForSwitch?[]:catalogue?.options||[])if(session.dataset.sessionId&&workspaceArchived.has(session.value)===showWorkspaceArchived){
          const row=make('div',undefined,'session-row'),button=make('button',session.textContent,'conversation-link'),toolbar=make('div',undefined,'session-toolbar hover-toolbar'),current=option.value===active&&session.value===originalSessions.value;row.dataset.sessionId=session.value;row.dataset.workspaceId=option.value;row.classList.toggle('selected',current);button.type='button';button.disabled=switching;button.title=session.dataset.sessionTitle||session.textContent;button.classList.toggle('selected',current);if(current)button.setAttribute('aria-current','page');button.onclick=()=>{if(option.value!==active){window.xMindView.postMessage({type:'workspace-session-select',workspace_id:option.value,id:session.value});return;}originalSessions.value=session.value;originalSessions.dispatchEvent(new Event('change',{bubbles:true}));};
          toolbar.setAttribute('role','toolbar');toolbar.setAttribute('aria-label','Session actions for '+button.title);
          for(const [action,label]of [['rename','Rename'],[workspaceArchived.has(session.value)?'restore':'archive',workspaceArchived.has(session.value)?'Restore':'Archive'],['delete','Delete']]){
            const control=make('button',undefined,'session-inline-action'+(action==='delete'?' danger':''));control.type='button';control.dataset.sessionActionId=session.value;control.dataset.action=action;control.title=label;control.setAttribute('aria-label',label+' '+button.title);control.append(actionIcon(action));
            control.disabled=option.value!==active||switching||!!pendingAction||byId('workspace-config-open').disabled||(action==='rename'&&byId('rename').hidden)||(['archive','restore'].includes(action)&&catalogue?.archiveAvailable!==true)||(['archive','delete'].includes(action)&&session.dataset.sessionActive==='true');control.onclick=event=>{event.stopPropagation();chooseSessionAction({id:session.value,title:session.dataset.sessionTitle||session.textContent},action);};toolbar.append(control);
          }
          row.append(button,toolbar);sessions.append(row);
        }
        if(!sessions.children.length)sessions.append(make('p',showWorkspaceArchived?'No archived sessions.':'No sessions yet.','empty-session-list'));
        group.append(sessions);
      }
      list.append(group);
    }
    if(focusedSession){const row=[...list.querySelectorAll('.session-row')].find(item=>item.dataset.sessionId===focusedSession&&item.dataset.workspaceId===focusedSessionWorkspace);row?.querySelector('.conversation-link')?.focus({preventScroll:true});if(focusedAction)row?.querySelector('[data-action="'+focusedAction+'"]')?.focus({preventScroll:true});}
    else if(focusedWorkspace){const row=[...list.querySelectorAll('.workspace-row')].find(item=>item.querySelector('.workspace-toggle').dataset.workspaceId===focusedWorkspace);row?.querySelector(workspaceAction==='new'?'.workspace-new-session':workspaceAction==='configure'?'.workspace-configure':workspaceAction==='expand'?'.workspace-disclosure':'.workspace-toggle')?.focus({preventScroll:true});}
  };
  const observer=new MutationObserver(refresh);observer.observe(byId('workspace-control'),{attributes:true,attributeFilter:['data-switching']});for(const id of ['workspace-picker','sessions'])observer.observe(byId(id),{childList:true,subtree:true,attributes:true});
  for(const id of ['workspace-config-open','add-workspace','rename'])observer.observe(byId(id),{attributes:true,attributeFilter:['disabled','hidden']});
  originalSessions.addEventListener('change',refresh);byId('workspace-picker').addEventListener('change',refresh);refresh();
  // Bound composer menus reuse the existing native select/change contracts.
  let openComposerMenu;
  for(const [id,width]of [['footer-provider',200],['model',260],['access-mode',180]]){
    const source=byId(id),shell=make('div',undefined,'composer-picker'),trigger=make('button',undefined,'composer-picker-trigger'),label=make('span'),popup=make('div',undefined,'composer-picker-menu');
    source.classList.add('composer-picker-source');shell.dataset.picker=id;trigger.type='button';trigger.id=id+'-trigger';trigger.setAttribute('role','combobox');trigger.setAttribute('aria-label',source.getAttribute('aria-label'));trigger.setAttribute('aria-haspopup','listbox');trigger.setAttribute('aria-controls',id+'-options');trigger.setAttribute('aria-expanded','false');
    const arrow=document.createElementNS('http://www.w3.org/2000/svg','svg'),path=document.createElementNS('http://www.w3.org/2000/svg','path');arrow.setAttribute('viewBox','0 0 16 16');arrow.setAttribute('aria-hidden','true');path.setAttribute('d','m4 6 4 4 4-4');arrow.append(path);trigger.append(label,arrow);shell.append(trigger);source.after(shell);
    popup.id=id+'-options';popup.hidden=true;popup.setAttribute('role','listbox');popup.setAttribute('aria-label',source.getAttribute('aria-label'));document.body.append(popup);
    let signature;
    const close=(focus=false)=>{popup.hidden=true;trigger.setAttribute('aria-expanded','false');if(openComposerMenu?.popup===popup)openComposerMenu=undefined;if(focus)trigger.focus();};
    const options=()=>[...popup.querySelectorAll('button:not(:disabled)')];
    const focusOption=button=>{button?.focus();button?.scrollIntoView?.({block:'nearest'});};
    const renderOptions=()=>{const focused=popup.contains(document.activeElement)?document.activeElement.dataset.value:undefined;popup.replaceChildren();for(const option of source.options){const item=make('button',option.textContent,'composer-picker-option');item.type='button';item.disabled=option.disabled;item.dataset.value=option.value;item.title=option.textContent;item.setAttribute('role','option');item.setAttribute('aria-selected',String(option.selected));item.onclick=()=>{if(source.disabled)return;if(source.value!==option.value){source.value=option.value;source.dispatchEvent(new Event('change',{bubbles:true}));}close(true);sync();};popup.append(item);}if(focused!==undefined)focusOption(options().find(item=>item.dataset.value===focused));};
    const sync=()=>{const next=JSON.stringify([source.hidden,source.disabled,source.value,source.title,[...source.options].map(option=>[option.value,option.textContent,option.disabled])]);if(signature===next)return;signature=next;shell.hidden=source.hidden;trigger.disabled=source.disabled;trigger.classList.toggle('full-access',id==='access-mode'&&source.value==='full');const selected=source.selectedOptions[0]?.textContent||'Choose';label.textContent=id==='access-mode'?(source.value==='full'?'Full access':'Default'):selected;trigger.title=selected+' · '+source.title;if(source.hidden||source.disabled)close();if(!popup.hidden){renderOptions();position();}};
    const position=()=>{const composerWidth=source.closest('.composer')?.getBoundingClientRect().width||width;popup.style.width=Math.min(width,window.innerWidth-16,composerWidth)+'px';const anchor=trigger.getBoundingClientRect(),height=Math.min(popup.getBoundingClientRect().height,260);popup.style.left=Math.max(8,Math.min(anchor.left,window.innerWidth-popup.getBoundingClientRect().width-8))+'px';popup.style.top=Math.max(8,anchor.top-height-5)+'px';};
    const open=()=>{if(trigger.disabled)return;openComposerMenu?.close();sync();renderOptions();popup.hidden=false;position();trigger.setAttribute('aria-expanded','true');openComposerMenu={popup,trigger,close};};
    trigger.onclick=()=>popup.hidden?open():close();trigger.onkeydown=event=>{if(['ArrowDown','ArrowUp','Home','End'].includes(event.key)){event.preventDefault();if(popup.hidden)open();const items=options();focusOption(event.key==='End'||event.key==='ArrowUp'?items.at(-1):items.find(item=>item.getAttribute('aria-selected')==='true')||items[0]);}else if(event.key==='Escape')close(true);};
    popup.onkeydown=event=>{if(event.key==='Escape'){event.preventDefault();close(true);}else if(event.key==='Tab')close();else if(['ArrowDown','ArrowUp','Home','End'].includes(event.key)){event.preventDefault();const items=options(),index=items.indexOf(document.activeElement),next=event.key==='Home'?0:event.key==='End'?items.length-1:(index+(event.key==='ArrowDown'?1:-1)+items.length)%items.length;focusOption(items[next]);}};
    new MutationObserver(sync).observe(source,{attributes:true,childList:true,subtree:true});source.addEventListener('change',sync);sync();
  }
  document.addEventListener('pointerdown',event=>{if(openComposerMenu&&!openComposerMenu.popup.contains(event.target)&&!openComposerMenu.trigger.contains(event.target))openComposerMenu.close();},{capture:true});
  document.addEventListener('focusin',event=>{if(openComposerMenu&&!openComposerMenu.popup.contains(event.target)&&event.target!==openComposerMenu.trigger)openComposerMenu.close();});
  window.addEventListener('resize',()=>openComposerMenu?.close());
  document.addEventListener('scroll',event=>{if(openComposerMenu&&!openComposerMenu.popup.contains(event.target))openComposerMenu.close();},{capture:true});
  // A bounded display projection of native events, never an execution owner.
  const liveActivity=make('section',undefined,'live-activity');liveActivity.setAttribute('aria-label','Live tool activity');
  const now=make('div',undefined,'activity-now'),nowDot=make('span',undefined,'activity-dot'),nowText=make('span','Ready');now.id='activity-now';now.setAttribute('role','status');now.setAttribute('aria-live','polite');nowDot.setAttribute('aria-hidden','true');now.append(nowDot,nowText);
  const runningList=make('div'),finished=make('details',undefined,'activity-finished'),finishedLabel=make('summary','Finished'),finishedList=make('div'),activityEmpty=make('p','Tool calls and file operations will appear here as xMind works.','inspector-empty');
  runningList.id='activity-running';finished.id='activity-finished';finished.append(finishedLabel,finishedList);finished.hidden=true;liveActivity.append(now,runningList,finished,activityEmpty);activity.prepend(liveActivity);
  const activityRows=new Map(),seenEvents=new Set();let activityRun,activityPhase='Ready',activityStage='',activityCounter=0,activityApprovals=0,outputFrame;
  const operationStates=new Map();let pendingMcpOperations=new Set();
  const syncOperationRows=()=>{for(const row of activityRows.values()){
    const matches=[...operationStates.values()].filter(item=>item.run===row.scope&&(row.isMcp?item.tool==='mcp_tool'&&item.alias===row.name:item.tool===(row.name==='edit_file'?'replace_file':row.name)&&item.path===row.args.path&&(!row.args.folder_id||item.folder===row.args.folder_id)&&(!item.profile||!row.args.profile||item.profile===row.args.profile))),prepared=matches.filter(item=>['awaiting_approval','executing','prepared'].includes(item.state)).at(-1),binding=matches.at(-1);
    if(row.isMcp){row.card.querySelector('strong').textContent=binding?.peerTool?(binding.peerTool+(binding.server?' · '+binding.server:'')):'MCP tool';}
    const phase=row.state==='running'?(prepared?.state==='awaiting_approval'?'waiting':prepared&&pendingMcpOperations.has(prepared.id)?'input':activityPhase==='paused'?'paused':'working'):row.state;
    row.card.dataset.phase=phase;
    const verbs={read_file:['Reading','Read'],list_files:['Listing','Listed'],search_files:['Searching','Searched'],create_file:['Creating','Created'],replace_file:['Updating','Updated'],patch_file:['Editing','Edited'],edit_file:['Editing','Edited'],run_process:['Running','Ran'],delete_file:['Deleting','Deleted']};
    const verb=(row.isMcp?['Calling','Called']:verbs[row.name]||[row.label,row.label])[row.state==='running'?0:1];row.stateLabel.textContent=phase==='waiting'?'Review':phase==='input'?'Input':phase==='paused'?'Paused':row.state==='failed'?'Failed':row.state==='interrupted'?'Interrupted':verb;
    row.summary.title=(row.isMcp?row.card.querySelector('strong').textContent:row.label)+(row.resource?' · '+row.resource:'')+' · '+(phase==='waiting'?'Waiting for approval':phase==='input'?'Waiting for input':phase);
    row.summary.setAttribute('aria-label',row.summary.title);row.card.classList.toggle('needs-review',phase==='waiting');
  }};
  const activePhase=()=>['queued','running','paused'].includes(activityPhase);
  const syncActivity=()=>{
    const executing=[...activityRows.values()].find(row=>row.state==='running'),activeCount=[...activityRows.values()].filter(row=>row.state==='running').length,busy=activePhase()||!!executing;
    const label=activityApprovals?'Waiting for approval · '+activityApprovals:pendingMcpOperations.size?'Waiting for input · '+pendingMcpOperations.size:activityPhase==='paused'?'Paused':executing?'Working'+(activeCount>1?' · '+activeCount:'…'):activityStage||(activityPhase==='running'?'Working…':activityPhase==='queued'?'Starting…':activityPhase==='paused'?'Paused':activityPhase==='completed'?'Completed':activityPhase==='failed'?'Run failed':activityPhase==='cancelled'?'Cancelled':'Ready');
    if(nowText.textContent!==label)nowText.textContent=label;now.dataset.state=activityApprovals||pendingMcpOperations.size||activityPhase==='paused'?'waiting':busy?'running':activityPhase==='failed'?'failed':'idle';
    byId('details-activity').dataset.live=String(busy||activityApprovals>0);activityEmpty.hidden=activityRows.size>0;finished.hidden=finishedList.children.length===0;finishedLabel.textContent='Finished · '+finishedList.children.length;
    syncOperationRows();
  };
  const clearActivity=()=>{activityRows.clear();seenEvents.clear();operationStates.clear();pendingMcpOperations.clear();runningList.replaceChildren();finishedList.replaceChildren();finished.open=false;activityStage='';activityApprovals=0;activityPhase='Ready';syncActivity();};
  const parseArguments=value=>{try{return typeof value==='string'?JSON.parse(value):value&&typeof value==='object'?value:{};}catch{return {};}};
  const toolLabel=name=>({'read_file':'Reading file','list_files':'Listing files','search_files':'Searching files','create_file':'Creating file','replace_file':'Updating file','patch_file':'Editing file','edit_file':'Editing file','run_process':'Command','delete_file':'Deleting file'}[name]||String(name||'Tool').replaceAll('_',' '));
  const resourceLabel=args=>{const command=typeof args.profile==='string'?[args.profile,...(Array.isArray(args.arguments)?args.arguments.slice(0,8).filter(value=>typeof value==='string').map(value=>value.slice(0,500)):[])].join(' · '):undefined,value=args.path||args.file_path||args.resource||args.uri||args.url||args.query||args.command||args.executable||command||args.working_directory||args.workdir;return typeof value==='string'?value.slice(0,300):'';};
  const renderActivityRow=(row,state,result)=>{
    const prior=row.state;row.state=state;row.card.dataset.state=state;row.stateLabel.textContent=state==='running'?'Running':state==='completed'?'Done':state==='failed'?'Failed':'Interrupted';
    if(state!=='running'&&prior==='running')row.card.open=false;
    if(state==='failed'||state==='interrupted'){runningList.append(row.card);}else if(state==='completed')finishedList.append(row.card);else runningList.append(row.card);
    row.detail.textContent=JSON.stringify(result===undefined?{tool:row.name,arguments:row.args}:{tool:row.name,arguments:row.args,result},null,2).slice(0,6000);
  };
  const trimActivity=()=>{while(activityRows.size>100){const oldest=[...activityRows].find(([,row])=>row.state!=='running');if(!oldest)break;oldest[1].card.remove();activityRows.delete(oldest[0]);}};
  const flushOutput=()=>{outputFrame=undefined;for(const row of activityRows.values())if(row.outputDirty){row.outputDirty=false;const follow=row.output.scrollHeight-row.output.scrollTop-row.output.clientHeight<24;row.output.hidden=false;row.output.textContent=row.tail;if(follow)row.output.scrollTop=row.output.scrollHeight;}};
  const handleActivityEvent=(event,child)=>{
    if(!event||typeof event.kind!=='string')return;
    const kind=event.kind,data=event.data||{},scope=child||event.run_id||activityRun||'root';
    if(!kind.startsWith('tool.')&&kind!=='process.output'){
      if(['run.queued','run.running','run.paused','run.completed','run.failed','run.cancelled'].includes(kind)){const phase=kind.slice(4);if(!child){activityPhase=phase;activityStage='';}if(['completed','failed','cancelled'].includes(phase))for(const row of activityRows.values())if(row.scope===scope&&row.state==='running')renderActivityRow(row,'interrupted');syncActivity();}
      else if(!child&&(kind==='model.text'||kind==='model.reasoning'||kind==='model.tool_delta')){const stage=kind==='model.tool_delta'?'Preparing tools…':kind==='model.text'?'Writing response…':'Thinking…';if(activityStage!==stage){activityStage=stage;syncActivity();}}
      return;
    }
    const receipt=Number.isSafeInteger(event.seq)?scope+':'+event.seq:undefined;if(receipt&&seenEvents.has(receipt))return;if(receipt){seenEvents.add(receipt);if(seenEvents.size>4096)seenEvents.delete(seenEvents.values().next().value);}
    if(kind==='process.output'){
      const stream=[...byId('process-streams').children].find(card=>card.dataset.operationId===data.operation_id),candidates=[...activityRows.values()].filter(row=>row.scope===scope&&row.name==='run_process'&&row.state==='running'&&(!row.operationId||row.operationId===data.operation_id));
      // Reuse the shared renderer's validated, offset-aware UTF-8 output.
      if(stream&&candidates.length===1){const row=candidates[0];row.operationId=data.operation_id;row.tail=[...stream.querySelectorAll('.process-output')].map(part=>part.querySelector('summary').textContent+'\n'+part.querySelector('pre').textContent).join('\n').slice(-4000);row.outputDirty=true;if(outputFrame===undefined)outputFrame=window.requestAnimationFrame?window.requestAnimationFrame(flushOutput):setTimeout(flushOutput,16);}
      return;
    }
    if(!['tool.started','tool.completed','tool.failed'].includes(kind))return;
    const identity=data.activity_id||data.call_id||data.plan_node_id;let key=identity?scope+':'+identity:undefined,row=key&&activityRows.get(key);
    if(!row&&kind!=='tool.started'&&!identity){const candidates=[...activityRows].filter(([,item])=>item.scope===scope&&item.name===data.name&&item.state==='running');if(candidates.length===1){key=candidates[0][0];row=candidates[0][1];}}
    if(!row){key=key||scope+':display-'+(++activityCounter);const args=parseArguments(data.arguments||data.arguments_json),card=make('details',undefined,'activity-item'),summary=make('summary'),dot=make('span',undefined,'activity-dot'),label=make('span',undefined,'activity-tool-label'),name=make('strong',/^mcp_[0-9a-f]{48}$/.test(data.name)?'MCP tool':typeof args.path==='string'?args.path.split(/[\\/]/).pop():data.name==='run_process'?args.profile||'Command':resourceLabel(args)||toolLabel(data.name)),stateLabel=make('span',undefined,'activity-tool-state'),detail=make('pre'),output=make('pre',undefined,'activity-output');dot.setAttribute('aria-hidden','true');dot.innerHTML='<svg viewBox="0 0 20 20" width="15" height="15" fill="none" stroke="currentColor" stroke-width="1.3" stroke-linejoin="round"><path d="M5 2h6l4 4v12H5Z M11 2v5h4 M7 10h6m-6 3h5"/></svg>';output.hidden=true;label.append(name);summary.append(dot,label,stateLabel);card.append(summary,output,detail);card.open=false;row={scope,name:data.name,isMcp:/^mcp_[0-9a-f]{48}$/.test(data.name),args,card,summary,detail,output,stateLabel,label:toolLabel(data.name),resource:resourceLabel(args),state:'running',tail:''};activityRows.set(key,row);}
    renderActivityRow(row,kind==='tool.started'?'running':kind==='tool.failed'?'failed':'completed',kind==='tool.started'?undefined:data.data);trimActivity();activityStage='';syncActivity();
  };
  window.addEventListener('message',event=>{
    if(event.source!==window||event.origin!==location.origin)return;const message=event.data;if(!message||typeof message!=='object')return;
    if(message.type==='history'||message.type==='reset-run'||message.type==='user'){clearActivity();if(message.type==='user'){activityRun=undefined;activityPhase='queued';syncActivity();}}
    else if(message.type==='runs'){if(activityRun!==message.selected){clearActivity();activityRun=message.selected;}activityPhase=message.runs?.find(run=>run.id===message.selected)?.state||'Ready';syncActivity();}
    else if(message.type==='status'&&['Ready','queued','running','paused','completed','failed','cancelled'].includes(message.text)){activityPhase=message.text;activityStage='';syncActivity();}
    else if(message.type==='operations'){const items=message.operations||[],ids=new Set(items.map(item=>item.id));for(const id of operationStates.keys())if(!ids.has(id))operationStates.delete(id);for(const item of items){const old=operationStates.get(item.id);if(old){old.state=item.state;continue;}const args=parseArguments(item.arguments_json);operationStates.set(item.id,{id:item.id,alias:args.alias,peerTool:args.peer_tool||args.peer_name,server:args.server_config_id,run:item.run_id,tool:item.tool,path:args.path,folder:args.folder_id,profile:args.profile||args.profile_id,state:item.state});}activityApprovals=items.filter(item=>item.state==='awaiting_approval').length;syncActivity();}
    else if(message.type==='mcp-inputs'){pendingMcpOperations=new Set((message.requests||[]).map(item=>item.operation_id));syncActivity();}
    else if(['event','owned-event','graph-event'].includes(message.type))handleActivityEvent(message.event,message.type==='event'?undefined:message.child_id||message.event?.run_id||'child');
  });
  syncActivity();
  // Native comparison controls stay in Changes; terminal remains independent.
  byId('workspace-vcs-diff').addEventListener('click',()=>choose('changes'));
})();
