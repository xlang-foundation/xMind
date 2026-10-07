// Independent official TypeScript SDK peer. No CPython execution.
import http from 'node:http';
import { Client } from '@modelcontextprotocol/sdk/client/index.js';
import { StreamableHTTPClientTransport } from '@modelcontextprotocol/sdk/client/streamableHttp.js';
import { McpServer } from '@modelcontextprotocol/sdk/server/mcp.js';
import { StreamableHTTPServerTransport } from '@modelcontextprotocol/sdk/server/streamableHttp.js';
import { z } from 'zod';

if (process.argv[2] === 'client') {
  const client = new Client({ name: 'independent-node-peer', version: '1' });
  await client.connect(new StreamableHTTPClientTransport(new URL(process.argv[3])));
  try {
    const tools = await client.listTools();
    if (!tools.tools.some(tool => tool.name === 'read_file')) throw new Error('read_file missing');
    const result = await client.callTool({ name: 'read_file', arguments: { path: 'README.md' } });
    if (result.isError || !result.structuredContent.content.includes('xMind')) throw new Error('Read failed');
    console.log('Official Node MCP client initialized, discovered and called xlang3 server tools');
  } finally { await client.close(); }
} else {
  const port = Number(process.argv[3] || 18766);
  const server = http.createServer(async (req, res) => {
    if (req.method !== 'POST') { res.writeHead(405); res.end(); return; }
    const mcp = new McpServer({ name: 'independent-node-peer', version: '1' });
    mcp.registerTool('echo', { description: 'Echo a string', inputSchema: { text: z.string() } },
      async ({ text }) => ({ content: [{ type: 'text', text }], structuredContent: { text } }));
    const transport = new StreamableHTTPServerTransport({ sessionIdGenerator: undefined, enableJsonResponse: true });
    try {
      await mcp.connect(transport);
      res.on('close', () => { transport.close(); mcp.close(); });
      await transport.handleRequest(req, res);
    } catch (error) { if (!res.headersSent) res.writeHead(500); res.end(); }
  });
  server.listen(port, '127.0.0.1', () => console.log(`Official Node MCP peer on ${port}`));
}
