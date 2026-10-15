'use strict';
// View validation only. Native owns path authorization, snapshots and effects.
(() => {
const text=value=>typeof value==='string'&&value.length>0&&value.length<=8192&&!/[\x00-\x1f]/.test(value);
const path=value=>text(value)&&!/^[/\\]|^[A-Za-z]:/.test(value)&&value.split(/[/\\]/).every(part=>part&&part!=='.'&&part!=='..');
const digest=value=>typeof value==='string'&&/^[a-f0-9]{64}$/.test(value);
const folders=value=>Array.isArray(value)&&value.length<=32&&value.every(path);
const need=(condition)=>{if(!condition)throw new Error('The patch proposal has malformed review snapshots.');};
function patchReview(operation){
 need(operation?.tool==='patch_file'&&typeof operation.arguments_json==='string');
 need(new TextEncoder().encode(operation.arguments_json).length<=2097152);
 const plan=JSON.parse(operation.arguments_json);
 need(plan&&typeof plan==='object'&&!Array.isArray(plan)&&['add','update','delete','move'].includes(plan.action));
 need(typeof plan.patch_id==='string'&&/^[A-Za-z0-9_.:-]{1,192}$/.test(plan.patch_id)&&Number.isSafeInteger(plan.patch_index)&&Number.isSafeInteger(plan.patch_file_count)&&plan.patch_file_count>=1&&plan.patch_file_count<=128&&plan.patch_index>=0&&plan.patch_index<plan.patch_file_count&&operation.id===plan.patch_id+'-'+plan.patch_index);
 need(path(plan.path)&&text(plan.parent_id)&&text(operation.workspace_id)&&typeof plan.before_content==='string'&&typeof plan.after_content==='string'&&new TextEncoder().encode(plan.before_content).length+new TextEncoder().encode(plan.after_content).length<=2097152);
 const add=plan.action==='add',remove=plan.action==='delete',move=plan.action==='move';
 need(plan.before_exists===!add&&plan.after_exists===!remove);
 if(add)need(plan.before_content===''&&folders(plan.create_directories));
 else need(text(plan.file_id)&&digest(plan.before_sha256));
 if(remove)need(plan.after_content==='');else need(digest(plan.after_sha256));
 if(move)need(path(plan.destination_path)&&plan.destination_path!==plan.path&&text(plan.destination_parent_id)&&folders(plan.create_directories));
 else need(plan.destination_path===undefined);
 if(plan.patch_files!==undefined){
  need(Array.isArray(plan.patch_files)&&plan.patch_files.length===plan.patch_file_count);
  need(new TextEncoder().encode(JSON.stringify(plan.patch_files)).length<=65536);
  for(const [index,file]of plan.patch_files.entries()){
   need(file&&file.operation_id===plan.patch_id+'-'+index&&['add','update','delete','move'].includes(file.action)&&path(file.path));
   if(file.action==='move')need(path(file.destination_path));
   if(file.create_directories!==undefined)need(folders(file.create_directories));
  }
  const current=plan.patch_files[plan.patch_index];need(current.action===plan.action&&current.path===plan.path&&(move?current.destination_path===plan.destination_path:current.destination_path===undefined));
 }else need(plan.patch_file_count===1);
 return plan;
}
function fileReview(operation){
 if(operation?.tool==='patch_file')return patchReview(operation);
 if(!['replace_file','create_file'].includes(operation?.tool))throw new Error('This operation does not contain a file change.');
 const plan=JSON.parse(operation.arguments_json);
 if(!path(plan.path)||typeof plan.before_content!=='string'||typeof plan.after_content!=='string'||new TextEncoder().encode(plan.before_content).length+new TextEncoder().encode(plan.after_content).length>2097152)throw new Error('The backend edit proposal has no comparable text snapshots.');
 if(operation.tool==='create_file'&&(plan.before_exists!==false||plan.before_content!==''))throw new Error('The creation proposal has no verified absent-file precondition.');
 return plan;
}
function canRestoreFileOperation(operation){
 try{
  if(!operation||operation.state!=='succeeded'||typeof operation.id!=='string'||!/^[A-Za-z0-9_.:-]{1,128}$/.test(operation.id)||typeof operation.workspace_id!=='string'||!operation.workspace_id||!['replace_file','create_file','patch_file'].includes(operation.tool))return false;
  const saved=operation.tool==='patch_file'?patchReview(operation):fileReview(operation),receipt=JSON.parse(operation.result_json);
  if(!saved||!receipt||typeof receipt!=='object'||Array.isArray(receipt)||saved.workspace_id!==operation.workspace_id||typeof saved.folder_id!=='string'||!saved.folder_id||!Number.isSafeInteger(saved.workspace_revision)||saved.workspace_revision<1||typeof saved.target_workspace_id!=='string'||!saved.target_workspace_id)return false;
  const hash=value=>digest(value),validPath=value=>typeof value==='string'&&value.length>0&&value.length<=8192;
  const action=operation.tool==='replace_file'?'update':operation.tool==='create_file'?'add':saved.action,path=saved.path;
  if(!validPath(path)||!['update','add','delete','move'].includes(action)||receipt.path!==(action==='move'?saved.destination_path:path))return false;
  if(operation.tool==='patch_file'&&(receipt.operation_id!==operation.id||receipt.state!=='succeeded'||receipt.action!==action))return false;
  if(operation.tool!=='patch_file'&&(!validPath(receipt.file_id)||receipt.path!==path))return false;
  if(action==='update')return typeof saved.file_id==='string'&&!!saved.file_id&&typeof saved.before_content==='string'&&typeof saved.after_content==='string'&&hash(saved.before_sha256)&&hash(saved.after_sha256)&&receipt.file_id===saved.file_id&&receipt.after_sha256===saved.after_sha256;
  if(action==='add')return typeof saved.after_content==='string'&&hash(saved.after_sha256)&&validPath(receipt.file_id)&&(receipt.after_sha256||receipt.content_sha256)===saved.after_sha256;
  if(action==='delete')return validPath(saved.file_id)&&typeof saved.before_content==='string'&&hash(saved.before_sha256)&&receipt.file_id===saved.file_id&&receipt.removed_sha256===saved.before_sha256;
  return validPath(saved.destination_path)&&typeof saved.file_id==='string'&&!!saved.file_id&&typeof saved.before_content==='string'&&typeof saved.after_content==='string'&&hash(saved.before_sha256)&&hash(saved.after_sha256)&&receipt.path===saved.destination_path&&receipt.source_path===path&&receipt.file_id===saved.file_id&&receipt.before_sha256===saved.before_sha256&&receipt.after_sha256===saved.after_sha256;
 }catch{return false;}
}
const api={patchReview,fileReview,canRestoreFileOperation};if(typeof module!=='undefined'&&module.exports)module.exports=api;else globalThis.XMindPatchReview=api;
})();
