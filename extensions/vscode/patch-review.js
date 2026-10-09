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
const api={patchReview,fileReview};if(typeof module!=='undefined'&&module.exports)module.exports=api;else globalThis.XMindPatchReview=api;
})();
