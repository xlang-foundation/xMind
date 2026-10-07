'use strict';
// Actual VS Code API test of the product snapshot provider/diff adapter.
// Labeled synthetic proposal bytes; no model, backend grant or workspace edit.
const assert=require('node:assert/strict');
const fs=require('node:fs');
const path=require('node:path');
const vscode=require('vscode');
const {editReview}=require('../extensions/vscode/edit-review');
exports.run=async()=>{
  const subscriptions=[];const output=process.env.XMIND_REVIEW_TEST_RESULT;
  if(!output) throw new Error('Missing review-test evidence destination');
  try {
    const review=editReview(vscode,{subscriptions});
    const proposal={id:'actual-host-fixture',tool:'replace_file',arguments_json:JSON.stringify({
      path:'labeled-test-fixture.cpp',before_content:'// Synthetic review fixture\nint value = 1;\n',
      after_content:'// Synthetic review fixture\nint value = 2;\n'})};
    await review(proposal);
    const deadline=Date.now()+5000;let tab;
    while(Date.now()<deadline) {
      tab=vscode.window.tabGroups.all.flatMap(group=>group.tabs).find(candidate=>candidate.input instanceof vscode.TabInputTextDiff&&candidate.input.modified.scheme==='xmind-review');
      if(tab) break;await new Promise(resolve=>setTimeout(resolve,50));
    }
    assert.ok(tab,'Native VS Code diff tab must open for the product proposal snapshots');
    const before=await vscode.workspace.openTextDocument(tab.input.original);
    const after=await vscode.workspace.openTextDocument(tab.input.modified);
    const plan=JSON.parse(proposal.arguments_json);
    assert.equal(before.getText(),plan.before_content);assert.equal(after.getText(),plan.after_content);
    await vscode.window.showTextDocument(after,{preview:true});
    assert.equal(vscode.window.activeTextEditor.document.uri.toString(),after.uri.toString());
    try {await vscode.commands.executeCommand('type',{text:'UNEXPECTED_MUTATION'});} catch {} // Some hosts reject the command explicitly.
    assert.equal(after.getText(),plan.after_content,'Editor typing must not mutate the read-only proposal snapshot');
    assert.equal(after.isDirty,false);
    await assert.rejects(vscode.workspace.fs.writeFile(after.uri,Buffer.from('UNEXPECTED_MUTATION')));
    assert.equal(after.getText(),plan.after_content);
    await review(proposal);
    fs.mkdirSync(path.dirname(output),{recursive:true});
    fs.writeFileSync(output,JSON.stringify({passed:true,vscode:vscode.version,scope:'Actual VS Code product snapshot provider and native diff; synthetic proposal, no live model or approved effect',checks:['native diff tab','exact before/after text','typing cannot mutate snapshot','filesystem writes rejected','snapshot remains clean']},null,2)+'\n');
    console.log('xMind actual VS Code read-only diff contract passed. Proposal content is a labeled fixture.');
  } finally {for(const item of subscriptions.reverse())item.dispose();}
};
