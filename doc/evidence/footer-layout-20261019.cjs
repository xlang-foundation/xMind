const fs=require('node:fs'),assert=require('node:assert/strict');
const {chromium}=require('C:/Users/shaw9/.cache/codex-runtimes/codex-primary-runtime/dependencies/node/node_modules/playwright');
const root='D:/CantorAI2026/xMind',out='C:/xMindNativeValidation/footer-layout-20261019';
fs.mkdirSync(out,{recursive:true});
const {html}=require(root+'/extensions/vscode/webview.js');
const {browserHtml}=require(root+'/views/browser/page.cjs');
const css=fs.readFileSync(root+'/extensions/vscode/media/chat.css','utf8');
const baseline=css.replace(/footer\{flex:0 1 auto;min-height:96px;max-height:45%;overflow:auto\}\r?\n/,'');
assert.notEqual(css,baseline,'baseline removes only the new general footer constraint');
(async()=>{
 const browser=await chromium.launch({executablePath:'C:/Program Files (x86)/Microsoft/Edge/Application/msedge.exe',headless:true,chromiumSandbox:true});
 const results=[];
 try{for(const mode of ['editor','browser'])for(const height of [480,720,1000])for(const version of ['baseline','fixed']){
  const page=await browser.newPage({viewport:{width:mode==='editor'?420:1440,height}});
  await page.route('**/*',route=>route.abort());
  let markup=mode==='browser'?browserHtml():html('fixture',{source:'https://fixture',css:'fixture.css',script:'fixture.js'});
  markup=markup.replace(/<script\b[^>]*>[\s\S]*?<\/script>/g,'').replace(/<meta http-equiv="Content-Security-Policy"[^>]*>/,'').replace(/<link\b[^>]*>/g,'');
  await page.setContent(markup);
  await page.addStyleTag({content:version==='fixed'?css:baseline});
  if(mode==='browser')await page.addStyleTag({content:fs.readFileSync(root+'/views/browser/browser.css','utf8')});
  await page.evaluate(()=>{
   document.querySelector('#empty').hidden=true;
   document.querySelector('#history').innerHTML=Array.from({length:20},(_,i)=>`<article class="message"><h4>Assistant ${i}</h4><p>Fixture conversation text for layout verification.</p></article>`).join('');
   for(const id of ['skills-view','context-view','agent-picker','workflow-picker']){const el=document.getElementById(id);el.hidden=false;el.open=true;}
   document.querySelector('#skill-list').innerHTML='<p>Attached fixture skill</p>'.repeat(8);
   document.querySelector('#context-status').innerHTML='<p>Fixture context information</p>'.repeat(5);
   document.querySelector('#prompt').style.height='180px';
   document.querySelector('#prompt').value='Fixture draft\n'.repeat(12);
   document.querySelector('#send').disabled=false;
  });
  const metrics=await page.evaluate(()=>{const main=document.querySelector('#scroll'),footer=document.querySelector('footer'),container=footer.parentElement;return {history:main.clientHeight,footer:footer.clientHeight,footerScroll:footer.scrollHeight,container:container.clientHeight,bodyScroll:document.body.scrollHeight,viewport:innerHeight};});
  results.push({mode,height,version,...metrics});
  if(version==='fixed'){
   assert(metrics.history>=height*.25,JSON.stringify(results.at(-1)));
   assert(metrics.bodyScroll<=height+1,'page must not overflow');
   await page.locator('#send').focus();
   const visible=await page.locator('#send').evaluate(el=>{const r=el.getBoundingClientRect(),f=el.closest('footer').getBoundingClientRect();return r.top>=f.top&&r.bottom<=f.bottom;});
   assert(visible,'keyboard focus reveals Send in scrollable footer');
   await page.screenshot({path:`${out}/${mode}-${height}.png`});
  }
  await page.close();
 }
 console.log(JSON.stringify(results));
 assert(results.some(r=>r.version==='baseline'&&r.height===720&&results.find(f=>f.mode===r.mode&&f.height===r.height&&f.version==='fixed').history>r.history+100),'reproduce normal-height history loss');
 fs.writeFileSync(out+'/result.json',JSON.stringify({scope:'Production HTML/CSS with synthetic expanded-control/history fixtures in sandboxed headless Edge. No backend, credentials, model, installed editor or external-browser acceptance.',passed:true,results},null,2));
 console.log(JSON.stringify(results));
 }finally{await browser.close();}
})().catch(error=>{console.error(error);process.exitCode=1;});
