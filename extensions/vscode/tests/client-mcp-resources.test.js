'use strict';
// Synthetic transport data checks only the thin browser/VS Code client boundary.
const test=require('node:test'),assert=require('node:assert/strict');
const {BackendClient,validateMcpResourceCatalog}=require('../client');
const token='synthetic-mcp-resource-owner'.padEnd(64,'x');
const catalog=()=>({servers:[{id:'tools.peer',revision:3,enabled:true,state:'connected',resource_count:1,template_count:1}],resources:[{server_id:'tools.peer',config_revision:3,resource:{uri:'fixture://readme',name:'README',mimeType:'text/plain',description:'Fixture metadata'}}],templates:[{server_id:'tools.peer',config_revision:3,template:{name:'by-name',uriTemplate:'fixture://{name}',description:'Fixture template'}}]});
test('MCP resource catalog reads the authenticated local route and validates configured-server bindings',async()=>{
 const seen=[],client=new BackendClient('http://127.0.0.1:8765',()=>token,async(url,options)=>{seen.push({url,options});return{ok:true,json:async()=>catalog()};});
 assert.deepEqual(await client.mcpResources(),catalog());assert.equal(seen[0].url,client.baseUrl+'/v1/mcp/resources');assert.equal(seen[0].options.method,'GET');assert.equal(seen[0].options.headers.Authorization,'Bearer '+token);assert.equal(seen[0].options.redirect,'error');
});
test('MCP resource catalogs reject private fields, invalid peer states, duplicate entries and revision drift',()=>{
 assert.equal(validateMcpResourceCatalog(catalog()).templates.length,1);
 for(const mutate of [v=>v.secret='credential',v=>v.servers[0].state='ready',v=>v.resources.push(v.resources[0]),v=>v.resources[0].config_revision++,v=>v.servers[0].resource_count=2,v=>v.resources[0].resource.uri='',v=>v.templates.push(v.templates[0]),v=>v.templates[0].template.uriTemplate='']){const value=catalog();mutate(value);assert.throws(()=>validateMcpResourceCatalog(value));}
});
