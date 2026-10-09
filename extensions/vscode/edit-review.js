'use strict';
const crypto=require('node:crypto');
const {fileReview}=require('./patch-review');

// Read-only proposal snapshots. Never reads or writes a workspace file, and
// never treats opening a comparison as permission to execute the proposal.
function editReview(vscode,context) {
  const documents=new Map();let bytes=0;
  context.subscriptions.push(vscode.workspace.registerTextDocumentContentProvider('xmind-review',{
    provideTextDocumentContent:uri=>documents.get(uri.toString())||''
  }));
  context.subscriptions.push({dispose(){documents.clear();bytes=0;}});
  return async operation=>{
    const plan=fileReview(operation);
    const size=Buffer.byteLength(plan.before_content)+Buffer.byteLength(plan.after_content);
    if(size>2097152) throw new Error('The edit proposal exceeds the comparison limit.');
    const identity=crypto.createHash('sha256').update(operation.id).update('\0').update(operation.arguments_json).digest('hex');
    const name=encodeURIComponent(plan.path.replaceAll('\\','/').split('/').at(-1)||'file');
    const before=vscode.Uri.parse(`xmind-review:/${identity}/before/${name}`);
    const afterName=encodeURIComponent((plan.destination_path||plan.path).replaceAll('\\','/').split('/').at(-1)||'file');
    const after=vscode.Uri.parse(`xmind-review:/${identity}/after/${afterName}`);
    if(!documents.has(before.toString())) {
      if(documents.size>=64 || bytes+size>33554432) throw new Error('Comparison snapshot limit reached. Reload the xMind extension to release old snapshots.');
      documents.set(before.toString(),plan.before_content);documents.set(after.toString(),plan.after_content);bytes+=size;
    }
    const action=operation.tool==='patch_file'?plan.action:operation.tool==='create_file'?'new file':'edit';
    const detail=action==='move'?plan.path+' → '+plan.destination_path:plan.path;
    const absence=action==='add'?' (before: absent)':action==='delete'?' (after: absent)':'';
    await vscode.commands.executeCommand('vscode.diff',before,after,`xMind proposed ${action}: ${detail}${absence}`,{preview:true});
  };
}
module.exports={editReview};
