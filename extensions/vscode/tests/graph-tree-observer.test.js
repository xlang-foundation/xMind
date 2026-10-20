'use strict';
const test=require('node:test'),assert=require('node:assert/strict');
const {observeGraphTree}=require('../client');
const run=(id,parent,node,graph=false)=>({id,session_id:'session',state:'running',parent_id:parent,node_id:node,graph_root:graph});
function fixture(){
 const leaf={nodes:[{id:'answer',type:'agent'}]},binding={id:'call',type:'graph',graph_id:'child',graph_revision:7,graph_spec:leaf};
 const root={run:run('root','','',true),spec:{nodes:[binding]}},nested={run:run('child','root','call',true),graph_id:'child',graph_revision:7,spec:leaf},agent=run('agent','child','answer');
 const batches={root:[nested.run],child:[agent]},records={child:nested},reads=[];
 return {root,nested,agent,batches,records,reads,client:{graphChildren:async id=>(reads.push(['children',id]),batches[id]||[]),graph:async id=>(reads.push(['graph',id]),records[id])}};
}
test('graph observer follows immutable nested edges and keeps root-relative labels',async()=>{const f=fixture(),o=await observeGraphTree(f.client,f.root,'session');assert.deepEqual(o.children.map(c=>c.id),['child','agent']);assert.deepEqual(o.paths.get('agent'),['call','answer']);assert.equal(o.definitions.get('agent').type,'agent');assert.equal(o.graphs[0].record,f.nested);assert.deepEqual(f.reads,[['children','root'],['graph','child'],['children','child']]);});
for(const [name,change]of [
 ['foreign session',f=>f.agent.session_id='foreign'],['wrong immediate parent',f=>f.agent.parent_id='root'],['undeclared node',f=>f.agent.node_id='unknown'],['cycle or duplicate identity',f=>f.agent.id='root'],['duplicate declared claim',f=>f.batches.child.push({...f.agent,id:'other'})],['incorrect child role',f=>f.agent.graph_root=true],['changed graph revision',f=>f.nested.graph_revision=8],['changed immutable child bytes',f=>f.nested.spec={nodes:[{id:'other',type:'agent'}]}]
])test('graph observer rejects '+name,async()=>{const f=fixture();change(f);await assert.rejects(observeGraphTree(f.client,f.root,'session'));});
test('graph observer rejects descendants beyond the backend invocation depth',async()=>{const f=fixture();let parent=f.nested;for(let depth=2;depth<=4;++depth){const spec={nodes:[]},node={id:'call',type:'graph',graph_id:'child'+depth,graph_revision:1,graph_spec:spec},child={run:run('child'+depth,parent.run.id,'call',true),graph_id:node.graph_id,graph_revision:1,spec};parent.spec.nodes=[node];f.batches[parent.run.id]=[child.run];f.records[child.run.id]=child;parent=child;}f.root.spec.nodes[0].graph_spec=f.nested.spec;await assert.rejects(observeGraphTree(f.client,f.root,'session'),/depth/);});
test('graph observer handles sibling node labels without conflating identities',async()=>{const f=fixture(),binding={...f.root.spec.nodes[0],id:'other',graph_id:'sibling'};f.root.spec.nodes.push(binding);const child={...f.nested,run:run('sibling','root','other',true),graph_id:'sibling'};f.batches.root.push(child.run);f.records.sibling=child;f.batches.sibling=[run('sibling-agent','sibling','answer')];const o=await observeGraphTree(f.client,f.root,'session');assert.deepEqual(o.paths.get('agent'),['call','answer']);assert.deepEqual(o.paths.get('sibling-agent'),['other','answer']);});


test('graph observer bounds expanded displayed node definitions as well as admitted runs',async()=>{const f=fixture(),spec={nodes:Array.from({length:100},(_,i)=>({id:'agent'+i,type:'agent'}))};f.root.spec.nodes=[];f.batches.root=[];for(let i=0;i<4;++i){const id='branch'+i;f.root.spec.nodes.push({id,type:'graph',graph_id:id,graph_revision:1,graph_spec:spec});const child={run:run(id,'root',id,true),graph_id:id,graph_revision:1,spec};f.records[id]=child;f.batches.root.push(child.run);f.batches[id]=[];}await assert.rejects(observeGraphTree(f.client,f.root,'session'),/Expanded/);});
