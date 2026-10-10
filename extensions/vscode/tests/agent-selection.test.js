'use strict';
const test=require('node:test');
const assert=require('node:assert/strict');
const {AgentSelectionController,validateAgentCatalogue,validateSessionAgent}=require('../client');

test('named agent selection pins public metadata per session, rejects stale replies and locks during a run',async()=>{
 const catalogue={agents:[{id:'review.agent',revision:2,model_id:'claude-sonnet'}]};
 assert.equal(validateAgentCatalogue(catalogue),catalogue);
 assert.throws(()=>validateAgentCatalogue({agents:[{...catalogue.agents[0],instructions:'private'}]}),/metadata/);
 let selection={session_id:'session-1',revision:4,editable:true,selected:null},writes=0;
 const client={
  async agentDefinitions(){return catalogue;},
  async sessionAgent(session){return validateSessionAgent(selection,session);},
  async replaceSessionAgent(session,definition,observed){assert.equal(session,'session-1');assert.equal(observed.revision,4);writes++;selection={session_id:session,revision:5,editable:true,selected:definition||null};return validateSessionAgent(selection,session,catalogue);}
 };
 const messages=[],scope={session:'session-1',generation:7,enabled:true};
 const controller=new AgentSelectionController(client,message=>messages.push(message),()=>scope);
 await controller.read();assert.equal(messages.at(-1).selection.revision,4);assert.equal(messages.at(-1).catalogue.agents[0].id,'review.agent');
 await controller.change({session:'session-1',revision:4,agent_id:'review.agent'});
 assert.equal(writes,1);assert.equal(messages.at(-1).selection.selected.revision,2);assert.equal(messages.at(-1).selection.revision,5);
 selection={...selection,editable:false};await controller.read();
 await assert.rejects(controller.change({session:'session-1',revision:5,agent_id:null}),/idle conversation/);assert.equal(writes,1);
 scope.session='session-2';await assert.rejects(controller.change({session:'session-1',revision:5,agent_id:null}),/idle conversation/);assert.equal(writes,1);
});
