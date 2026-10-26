'use strict';
// Browser-only arrangement of the shared view. Existing controllers keep sole
// ownership of workspace/session selection and folder management.
(() => {
  const byId=id=>document.getElementById(id),make=(tag,text,cls)=>{const value=document.createElement(tag);if(text)value.textContent=text;if(cls)value.className=cls;return value;};
  const layout=document.querySelector('.browser-layout'),chat=byId('agent-sidebar'),files=document.querySelector('.runtime-pane');
  if(!layout||!chat||!files)return;
  document.body.classList.add('conversation-layout');
  const navigation=make('aside',undefined,'workspace-navigation');navigation.setAttribute('aria-label','Workspaces and conversations');
  const heading=make('header'),title=make('strong','Workspaces'),add=make('button','＋');add.type='button';add.setAttribute('aria-label','Add workspace');add.title='Create workspace from a folder';add.onclick=()=>byId('add-workspace').click();heading.append(title,add);navigation.append(heading);
  const list=make('nav');list.id='workspace-conversations';list.setAttribute('aria-label','Workspace conversations');navigation.append(list);
  layout.prepend(navigation);
  const inspector=make('aside',undefined,'context-inspector');inspector.id='context-inspector';inspector.setAttribute('aria-label','Workspace details');
  const controls=make('header',undefined,'inspector-controls'),tabs=make('div');tabs.setAttribute('role','tablist');tabs.setAttribute('aria-label','Workspace detail views');
  const close=make('button','×');close.type='button';close.setAttribute('aria-label','Hide details');
  const panels=make('div',undefined,'inspector-panels'),activity=make('section');activity.id='inspector-activity';activity.setAttribute('role','tabpanel');activity.setAttribute('aria-labelledby','details-activity');
  files.id='inspector-files';files.setAttribute('role','tabpanel');files.setAttribute('aria-labelledby','details-files');
  const selection=make('div',undefined,'inspector-run');for(const id of ['run-picker','run-context','context-view'])if(byId(id))selection.append(byId(id));activity.append(selection,byId('run-activity'));
  activity.prepend(document.querySelector('.conversation-tools'));
  for(const id of ['skills-view','agent-picker','workflow-picker'])if(byId(id))activity.append(byId(id));
  panels.append(files,activity);inspector.append(controls,panels);layout.append(inspector);
  const toggle=make('button','Details');toggle.id='details-toggle';toggle.type='button';toggle.setAttribute('aria-controls',inspector.id);toggle.setAttribute('aria-expanded','true');toggle.title='Show or hide workspace files and activity';document.querySelector('.browser-bar').append(toggle);
  let selected='files';
  const choose=name=>{selected=name;files.hidden=name!=='files';activity.hidden=name!=='activity';if(name==='activity')byId('run-activity').open=true;for(const button of tabs.children){button.setAttribute('aria-selected',String(button.dataset.panel===name));button.tabIndex=button.dataset.panel===name?0:-1;}inspector.hidden=false;toggle.setAttribute('aria-expanded','true');};
  for(const [name,label] of [['files','Files'],['activity','Activity']]){const tab=make('button',label);tab.type='button';tab.id='details-'+name;tab.dataset.panel=name;tab.setAttribute('role','tab');tab.setAttribute('aria-controls',name==='files'?files.id:activity.id);tab.onclick=()=>choose(name);tab.onkeydown=event=>{if(['ArrowLeft','ArrowRight','Home','End'].includes(event.key)){event.preventDefault();const next=name==='files'?'activity':'files';choose(next);byId('details-'+next).focus();}};tabs.append(tab);}
  controls.append(tabs,close);close.onclick=()=>{inspector.hidden=true;toggle.setAttribute('aria-expanded','false');toggle.focus();};toggle.onclick=()=>{if(inspector.hidden)choose(selected);else close.click();};
  choose('files');
  // Approval review reveals its actual controls even when details were hidden.
  byId('approval-review').addEventListener('click',()=>choose('activity'));
  byId('failure-review').addEventListener('click',()=>choose('activity'));
  const originalSessions=byId('sessions');originalSessions.classList.add('navigation-source');
  const refresh=()=>{
    const picker=byId('workspace-picker'),active=picker.value;list.replaceChildren();add.disabled=byId('add-workspace').disabled;
    for(const option of picker.options){
      const name=option.dataset.workspaceName||option.textContent;
      const group=make('section',undefined,'workspace-group'),row=make('div',undefined,'workspace-row'),select=make('button',name),configure=make('button','⚙');
      select.type='button';select.title=option.title||option.textContent;select.setAttribute('aria-expanded',String(option.value===active));select.classList.toggle('selected',option.value===active);
      select.onclick=()=>{if(picker.value!==option.value){picker.value=option.value;picker.dispatchEvent(new Event('change',{bubbles:true}));}};
      configure.type='button';configure.setAttribute('aria-label','Configure '+option.textContent+' folders');configure.title='Manage workspace folders';configure.disabled=option.value!==active||byId('workspace-config-open').disabled;configure.onclick=()=>byId('workspace-config-open').click();row.append(select,configure);group.append(row);
      if(option.value===active){
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
