'use strict';
const test=require('node:test'),assert=require('node:assert/strict');
test('MCP login browser routes expose only exact observation, start and cancel methods',async()=>{
  const {allowedApiRoute}=await import('../server.mjs');
  const reads=['/v1/mcp/authorization/servers','/v1/mcp/authorization/attempts/login-1'];
  const writes=['/v1/mcp/authorization/attempts','/v1/mcp/authorization/attempts/login-1/cancel'];
  for(const route of [...reads,...writes])for(const method of ['GET','POST','HEAD','PUT','PATCH','DELETE'])assert.equal(allowedApiRoute(route,method),reads.includes(route)?method==='GET':method==='POST',route+' '+method);
  for(const route of ['/v1/mcp/authorization/servers/','/v1/mcp/authorization/attempts/login-1/complete','/v1/mcp/authorization/attempts/login-1/token','/v1/mcp/authorization/attempts/login-1/cancel/extra','/v1/mcp/authorization/attempts/foreign%2Fid','/v1/mcp/authorization/attempts/..','/v1/mcp/authorization/attempts/'+'x'.repeat(129),'/v1/mcp/credentials','/v1/mcp/configuration'])for(const method of ['GET','POST'])assert.equal(allowedApiRoute(route,method),false);
});
