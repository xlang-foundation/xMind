'use strict';
const test=require('node:test');
const assert=require('node:assert/strict');
const {GitChangesTreeProvider}=require('../git-changes-tree');

class EventEmitter {
  listeners=[];
  event=listener=>{this.listeners.push(listener);return {dispose:()=>{this.listeners=this.listeners.filter(value=>value!==listener);}};};
  fire(value){for(const listener of this.listeners)listener(value);}
}
class TreeItem { constructor(label,collapsibleState){this.label=label;this.collapsibleState=collapsibleState;} }
const vscode={EventEmitter,TreeItem,TreeItemCollapsibleState:{None:0,Expanded:2},ThemeIcon:class ThemeIcon{constructor(id){this.id=id;}}};

test('Git Changes groups native status by registered workspace folder',()=>{
  const provider=new GitChangesTreeProvider(vscode),updates=[];provider.onDidChangeTreeData(value=>updates.push(value));
  provider.setFolders([{name:'TestProj',root:'D:\\Projects\\TestProj',folder_id:'primary',ordinal:0,changes:[{file:'src/app.py',status:'modified',additions:4,deletions:2},{file:'README.md',status:'added',additions:8,deletions:0}]},{name:'Shared',root:'D:\\Projects\\Shared',folder_id:'shared',ordinal:1,changes:[]}]);
  assert.deepEqual(updates,[undefined]);
  const roots=provider.getChildren();assert.equal(roots.length,2);assert.equal(roots[0].label,'TestProj');assert.equal(roots[0].description,'D:\\Projects\\TestProj');
  const files=provider.getChildren(roots[0]);assert.deepEqual(files.map(item=>item.label),['src/app.py','README.md']);assert.equal(files[0].description,'+4  −2');assert.equal(files[0].iconPath.id,'diff-modified');assert.equal(files[1].iconPath.id,'diff-added');
  assert.equal(provider.getChildren(roots[1])[0].label,'No changed files');
});

test('Git Changes refuses malformed native snapshots and clears stale rows on disconnect',()=>{
  const provider=new GitChangesTreeProvider(vscode);provider.setFolders([{name:'TestProj',root:'D:\\Projects\\TestProj',changes:[{file:'app.py',status:'modified',additions:1,deletions:0}]}]);
  assert.throws(()=>provider.setFolders([{name:'TestProj',root:'D:\\Projects\\TestProj',changes:[{file:'app.py',status:'renamed',additions:1,deletions:0}]}]),/Invalid Git file snapshot/);
  provider.clear('Workspace disconnected.');assert.equal(provider.getChildren()[0].label,'Workspace disconnected.');
});
