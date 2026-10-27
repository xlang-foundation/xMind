'use strict';
// Browser-only arrangement of the shared view. Existing controllers keep sole
// ownership of workspace/session selection and folder management.
(() => {
  const byId=id=>document.getElementById(id),make=(tag,text,cls)=>{const value=document.createElement(tag);if(text)value.textContent=text;if(cls)value.className=cls;return value;};
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
  panels.append(files,activity);inspector.append(controls,panels);layout.append(inspector);
  const toggle=make('button',undefined,'sidebar-toggle');toggle.append(sidebarIcon('right'));toggle.id='details-toggle';toggle.type='button';toggle.setAttribute('aria-label','Toggle right sidebar');toggle.setAttribute('aria-controls',inspector.id);toggle.setAttribute('aria-expanded','true');toggle.title='Hide right sidebar';
  const leftToggle=make('button',undefined,'sidebar-toggle');leftToggle.append(sidebarIcon('left'));leftToggle.id='workspace-toggle';leftToggle.type='button';leftToggle.setAttribute('aria-label','Toggle left sidebar');leftToggle.setAttribute('aria-controls',navigation.id);leftToggle.setAttribute('aria-expanded','true');leftToggle.title='Hide left sidebar';
  const toolbar=document.querySelector('.browser-bar');toolbar.setAttribute('role','toolbar');toolbar.setAttribute('aria-label','Global toolbar');
  toolbar.prepend(leftToggle);
  const context=make('div',undefined,'header-context'),workspaceContext=make('button',undefined,'header-workspace'),separator=make('span','/','header-context-separator'),sessionContext=make('button',undefined,'header-session'),sessionLabel=make('span',undefined,'header-session-label');
  workspaceContext.id='current-workspace';workspaceContext.type='button';workspaceContext.setAttribute('aria-label','Workspace settings');workspaceContext.onclick=()=>byId('workspace-config-open').click();
  sessionContext.id='current-session';sessionContext.type='button';sessionContext.setAttribute('aria-label','Rename current session');sessionContext.onclick=()=>byId('rename').click();
  const renameIcon=byId('rename').querySelector('svg').cloneNode(true);sessionContext.append(sessionLabel,renameIcon);context.append(workspaceContext,separator,sessionContext);toolbar.querySelector('strong').after(context);
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
  const collapsedWorkspaces=new Set();let navigationSignature;
  const refresh=()=>{
    const picker=byId('workspace-picker'),active=picker.value;if(archiveWorkspace!==active){showArchived=false;archiveWorkspace=active;sessionNotice.hidden=true;}
    const switching=byId('workspace-control').dataset.switching==='true';navigation.setAttribute('aria-busy',String(switching));
    const signature=JSON.stringify([active,switching,showArchived,[...collapsedWorkspaces],!!pendingAction,byId('add-workspace').disabled,byId('workspace-config-open').disabled,byId('rename').disabled,byId('rename').hidden,originalSessions.value,originalSessions.dataset.archivedSessionIds,originalSessions.dataset.archiveAvailable,[...picker.options].map(option=>[option.value,option.textContent,option.title,option.dataset.workspaceName]),[...originalSessions.options].map(option=>[option.value,option.textContent,option.dataset.sessionId,option.dataset.sessionTitle,option.dataset.sessionActive])]);
    if(signature===navigationSignature)return;navigationSignature=signature;
    const focused=document.activeElement,focusedSession=focused.closest('.session-row')?.dataset.sessionId,focusedWorkspace=focused.closest('.workspace-row')?.querySelector('.workspace-toggle')?.dataset.workspaceId,focusedAction=focused.dataset.action,workspaceAction=focused.classList.contains('workspace-new-session')?'new':focused.classList.contains('workspace-configure')?'configure':'select';
    list.replaceChildren();add.disabled=byId('add-workspace').disabled;
    const archived=new Set(JSON.parse(originalSessions.dataset.archivedSessionIds||'[]')),archiveCount=[...originalSessions.options].filter(item=>archived.has(item.value)).length;archiveToggle.hidden=originalSessions.dataset.archiveAvailable!=='true'||(!showArchived&&archiveCount===0);archiveToggle.textContent='Archived sessions ('+archiveCount+')';archiveToggle.setAttribute('aria-pressed',String(showArchived));
    const workspace=picker.selectedOptions[0],session=originalSessions.selectedOptions[0];
    workspaceContext.textContent=workspace?.dataset.workspaceName||workspace?.textContent||'Workspace';workspaceContext.title=workspaceContext.textContent;workspaceContext.disabled=switching||byId('workspace-config-open').disabled;
    sessionLabel.textContent=session?.dataset.sessionTitle||session?.textContent||'New session';sessionContext.title=sessionLabel.textContent;sessionContext.disabled=switching||byId('rename').disabled||byId('rename').hidden;
    for(const option of picker.options){
      const name=option.dataset.workspaceName||option.textContent;
      const group=make('section',undefined,'workspace-group'),row=make('div',undefined,'workspace-row'),select=make('button',undefined,'workspace-toggle'),configure=make('button',undefined,'workspace-configure');configure.append(actionIcon('settings'));
      const icon=folderIcon(18);icon.classList.add('workspace-folder-icon');const label=make('span',name,'workspace-name'),chevron=document.createElementNS('http://www.w3.org/2000/svg','svg'),arrow=document.createElementNS('http://www.w3.org/2000/svg','path');for(const [key,value] of Object.entries({viewBox:'0 0 24 24',width:'12',height:'12',fill:'none',stroke:'currentColor','stroke-width':'1.8','aria-hidden':'true'}))chevron.setAttribute(key,value);arrow.setAttribute('d','m9 5 7 7-7 7');chevron.append(arrow);chevron.classList.add('workspace-chevron');select.append(icon,label,chevron);
      const expanded=option.value===active&&!collapsedWorkspaces.has(option.value);
      select.type='button';select.disabled=switching;select.setAttribute('aria-busy',String(switching&&option.value===active));select.dataset.workspaceId=option.value;select.title=option.title||option.textContent;select.setAttribute('aria-expanded',String(expanded));select.classList.toggle('selected',option.value===active);
      select.onclick=()=>{if(picker.value!==option.value){collapsedWorkspaces.delete(option.value);picker.value=option.value;picker.dispatchEvent(new Event('change',{bubbles:true}));}else{if(collapsedWorkspaces.has(option.value))collapsedWorkspaces.delete(option.value);else collapsedWorkspaces.add(option.value);refresh();[...list.querySelectorAll('.workspace-toggle')].find(button=>button.dataset.workspaceId===option.value)?.focus();}};
      const fresh=make('button',undefined,'workspace-new-session');fresh.type='button';fresh.append(plus.cloneNode(true));fresh.setAttribute('aria-label','New session in '+name);fresh.title='New session';fresh.disabled=switching||option.value!==active;fresh.onclick=()=>{showArchived=false;byId('new').click();};
      configure.type='button';configure.setAttribute('aria-label','Configure '+option.textContent+' folders');configure.title='Manage workspace folders';configure.disabled=switching||option.value!==active||byId('workspace-config-open').disabled;configure.onclick=()=>byId('workspace-config-open').click();const toolbar=make('div',undefined,'workspace-toolbar hover-toolbar');toolbar.setAttribute('role','toolbar');toolbar.setAttribute('aria-label','Workspace actions for '+name);toolbar.append(fresh,configure);row.append(select,toolbar);group.append(row);
      if(expanded){
        const sessions=make('div',undefined,'workspace-sessions');
        for(const session of originalSessions.options)if(session.dataset.sessionId&&archived.has(session.value)===showArchived){
          const row=make('div',undefined,'session-row'),button=make('button',session.textContent,'conversation-link'),toolbar=make('div',undefined,'session-toolbar hover-toolbar');row.dataset.sessionId=session.value;row.classList.toggle('selected',session.value===originalSessions.value);button.type='button';button.disabled=switching;button.title=session.dataset.sessionTitle||session.textContent;button.classList.toggle('selected',session.value===originalSessions.value);if(session.value===originalSessions.value)button.setAttribute('aria-current','page');button.onclick=()=>{originalSessions.value=session.value;originalSessions.dispatchEvent(new Event('change',{bubbles:true}));};
          toolbar.setAttribute('role','toolbar');toolbar.setAttribute('aria-label','Session actions for '+button.title);
          for(const [action,label]of [['rename','Rename'],[archived.has(session.value)?'restore':'archive',archived.has(session.value)?'Restore':'Archive'],['delete','Delete']]){
            const control=make('button',undefined,'session-inline-action'+(action==='delete'?' danger':''));control.type='button';control.dataset.sessionActionId=session.value;control.dataset.action=action;control.title=label;control.setAttribute('aria-label',label+' '+button.title);control.append(actionIcon(action));
            control.disabled=switching||!!pendingAction||byId('workspace-config-open').disabled||(action==='rename'&&byId('rename').hidden)||(['archive','restore'].includes(action)&&originalSessions.dataset.archiveAvailable!=='true')||(['archive','delete'].includes(action)&&session.dataset.sessionActive==='true');control.onclick=event=>{event.stopPropagation();chooseSessionAction({id:session.value,title:session.dataset.sessionTitle||session.textContent},action);};toolbar.append(control);
          }
          row.append(button,toolbar);sessions.append(row);
        }
        if(showArchived&&!sessions.children.length)sessions.append(make('p','No archived sessions.','empty-session-list'));
        group.append(sessions);
      }
      list.append(group);
    }
    if(focusedSession){const row=[...list.querySelectorAll('.session-row')].find(item=>item.dataset.sessionId===focusedSession);row?.querySelector('.conversation-link')?.focus({preventScroll:true});if(focusedAction)row?.querySelector('[data-action="'+focusedAction+'"]')?.focus({preventScroll:true});}
    else if(focusedWorkspace){const row=[...list.querySelectorAll('.workspace-row')].find(item=>item.querySelector('.workspace-toggle').dataset.workspaceId===focusedWorkspace);row?.querySelector(workspaceAction==='new'?'.workspace-new-session':workspaceAction==='configure'?'.workspace-configure':'.workspace-toggle')?.focus({preventScroll:true});}
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
  // A comparison or terminal request opens Files without submitting a command.
  for(const id of ['workspace-terminal-toggle','workspace-vcs-diff'])byId(id).addEventListener('click',()=>choose('files'));
})();
