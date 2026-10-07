'use strict';
const crypto=require('node:crypto');

// Read-only proposal snapshots. Never reads or writes a workspace file, and
// never treats opening a comparison as permission to execute the proposal.
function editReview(vscode,context) {
  const documents=new Map();let bytes=0;
  context.subscriptions.push(vscode.workspace.registerTextDocumentContentProvider('xmind-review',{
    provideTextDocumentContent:uri=>documents.get(uri.toString())||''
  }));
  context.subscriptions.push({dispose(){documents.clear();bytes=0;}});
  return async operation=>{
    if(operation.tool!=='replace_file') throw new Error('This operation does not contain a file edit.');
    const plan=JSON.parse(operation.arguments_json);
    if(typeof plan.path!=='string' || typeof plan.before_content!=='string' || typeof plan.after_content!=='string')
      throw new Error('The backend edit proposal has no comparable text snapshots.');
    const size=Buffer.byteLength(plan.before_content)+Buffer.byteLength(plan.after_content);
    if(size>2097152) throw new Error('The edit proposal exceeds the comparison limit.');
    const identity=crypto.createHash('sha256').update(operation.id).update('\0').update(operation.arguments_json).digest('hex');
    const name=encodeURIComponent(plan.path.replaceAll('\\','/').split('/').at(-1)||'file');
    const before=vscode.Uri.parse(`xmind-review:/${identity}/before/${name}`);
    const after=vscode.Uri.parse(`xmind-review:/${identity}/after/${name}`);
    if(!documents.has(before.toString())) {
      if(documents.size>=64 || bytes+size>33554432) throw new Error('Comparison snapshot limit reached. Reload the xMind extension to release old snapshots.');
      documents.set(before.toString(),plan.before_content);documents.set(after.toString(),plan.after_content);bytes+=size;
    }
    await vscode.commands.executeCommand('vscode.diff',before,after,`xMind proposed edit: ${plan.path}`,{preview:true});
  };
}
module.exports={editReview};
