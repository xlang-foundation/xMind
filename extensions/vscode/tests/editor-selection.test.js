'use strict';
// Synthetic editor/path metadata fixtures. No native backend, provider, real
// credential, installed IDE or workspace file is accessed by these tests.
const test=require('node:test'),assert=require('node:assert/strict');
const {captureEditorSelection}=require('../editor-selection');
const metadata={configured:true,root:'D:\\Repo',workspace_id:'windows-local-file-v1:1:2',authority_id:'b'.repeat(32)};
function fixture(text='const selected = 1;'){
  let reads=0;const editor={document:{uri:{scheme:'file',fsPath:'D:\\Repo\\src\\main.js',query:'',fragment:''},version:7,isDirty:false,isClosed:false,languageId:'javascript',getText:()=>{reads++;return text;}},selection:{start:{line:2,character:3},end:{line:4,character:5}}};
  const dependencies={realpath:async value=>value,stat:async()=>({isFile:()=>true,nlink:1})};return {editor,dependencies,reads:()=>reads};
}
test('selection creates an exact relative data draft without claiming a filesystem snapshot',async()=>{
 const f=fixture(),draft=await captureEditorSelection(f.editor,metadata,f.dependencies);assert.equal(draft.path,'src/main.js');assert.equal(draft.workspace_id,metadata.workspace_id);assert.match(draft.text,/Range: 3:4–5:6/);assert.match(draft.text,/saved editor buffer; not a backend filesystem snapshot/);assert.equal(draft.text.split('Selected text (JSON string):\n')[1],JSON.stringify('const selected = 1;'));assert.equal(f.reads(),1);assert.ok(!draft.text.includes(metadata.root));
});
test('unsaved Unicode buffer text is preserved and labelled without a save action',async()=>{
 const text='未保存 🌍\n```\n<script>selected()</script>',f=fixture(text);f.editor.document.isDirty=true;const draft=await captureEditorSelection(f.editor,metadata,f.dependencies);assert.match(draft.text,/Source: unsaved editor buffer/);assert.equal(JSON.parse(draft.text.split('Selected text (JSON string):\n')[1]),text);assert.equal(f.editor.document.version,7);
});
test('virtual, empty, private and linked selections are rejected before buffer reads',async()=>{
 for(const mutate of [f=>f.editor.document.uri.scheme='untitled',f=>f.editor.document.uri.query='secret',f=>f.editor.selection.end={...f.editor.selection.start},f=>f.editor.document.uri.fsPath='D:\\Repo\\.config\\providers.yaml',f=>f.editor.document.uri.fsPath='D:\\Repo\\.AGENTFLOW.\\state.txt',f=>f.dependencies.stat=async()=>({isFile:()=>true,nlink:2}),f=>f.dependencies.stat=async()=>({isFile:()=>false,nlink:1})]){const f=fixture();mutate(f);await assert.rejects(captureEditorSelection(f.editor,metadata,f.dependencies));assert.equal(f.reads(),0);}
});
test('sibling roots, aliases and case-sensitive canonical siblings cannot supply context',async()=>{
 for(const file of ['D:\\Elsewhere\\secret.txt','D:\\Repository\\main.js','D:\\repo\\main.js','D:\\Repo\\.config\\providers.yaml']){const f=fixture();f.dependencies.realpath=async value=>value===metadata.root?metadata.root:file;await assert.rejects(captureEditorSelection(f.editor,metadata,f.dependencies));assert.equal(f.reads(),0);}
 const outside=fixture();outside.editor.document.uri.fsPath='D:\\Elsewhere\\main.js';await assert.rejects(captureEditorSelection(outside.editor,metadata,outside.dependencies));assert.equal(outside.reads(),0);
});
test('editor version and selection changes during filesystem observation are rejected',async()=>{
 for(const change of [f=>f.editor.document.version++,f=>f.editor.selection.end.character++,f=>f.editor.document.isClosed=true]){const f=fixture();f.dependencies.stat=async()=>{change(f);return {isFile:()=>true,nlink:1};};await assert.rejects(captureEditorSelection(f.editor,metadata,f.dependencies),/changed/);assert.equal(f.reads(),0);}
});
test('oversized and malformed selected text is rejected without silent truncation',async()=>{
 for(const text of ['x'.repeat(32769),'\ud800','nul\0byte']){const f=fixture(text);await assert.rejects(captureEditorSelection(f.editor,metadata,f.dependencies),/valid text/);assert.equal(f.reads(),1);}
});
test('a retargeted editor path is rejected after reading the original buffer',async()=>{
 const f=fixture();let calls=0;f.dependencies.realpath=async value=>value===metadata.root?value:++calls===1?value:'D:\\Elsewhere\\secret.txt';await assert.rejects(captureEditorSelection(f.editor,metadata,f.dependencies),/file path changed/);assert.equal(f.reads(),1);
});
test('unconfigured, replaced workspace and stale adapter observations yield no context',async()=>{
 const f=fixture();await assert.rejects(captureEditorSelection(f.editor,{configured:false,root:null,workspace_id:null,authority_id:null},f.dependencies),/Connect/);assert.equal(f.reads(),0);
 f.dependencies.realpath=async value=>value===metadata.root?'D:\\Replaced':value;await assert.rejects(captureEditorSelection(f.editor,metadata,f.dependencies),/workspace path changed/);assert.equal(f.reads(),0);
 const stale=fixture();stale.dependencies.isCurrent=()=>false;await assert.rejects(captureEditorSelection(stale.editor,metadata,stale.dependencies),/changed/);assert.equal(stale.reads(),0);
});
