"""MCP Streamable HTTP client with explicit remote-tool allowlists."""

import json
import httpx
from agentflow.mcp import VERSIONS


class MCPClient:
    def __init__(self, url, headers=None, transport=None):
        self.url = url
        self.headers = {"Accept": "application/json, text/event-stream", **(headers or {})}
        self.client = httpx.AsyncClient(timeout=60, transport=transport, follow_redirects=False)
        self.counter = 0
        self.version = None
        self.session_id = None

    async def close(self):
        if self.session_id:
            try:
                await self.client.delete(self.url, headers=self._headers())
            finally:
                await self.client.aclose()
        else:
            await self.client.aclose()

    def _headers(self):
        headers = dict(self.headers)
        if self.version:
            headers["MCP-Protocol-Version"] = self.version
        if self.session_id:
            headers["MCP-Session-Id"] = self.session_id
        return headers

    async def notify(self, method, params=None):
        body = {"jsonrpc": "2.0", "method": method}
        if params is not None:
            body["params"] = params
        response = await self.client.post(self.url, json=body, headers=self._headers())
        response.raise_for_status()
        if response.status_code != 202:
            raise ValueError("MCP notification was not accepted")

    def _result(self, message, request_id):
        if message.get("jsonrpc") != "2.0" or message.get("id") != request_id:
            raise ValueError("MCP response ID or JSON-RPC version mismatch")
        if "error" in message:
            raise ValueError("MCP error: " + str(message["error"].get("code")))
        if "result" not in message:
            raise ValueError("MCP response has no result")
        return message["result"]

    async def rpc(self, method, params=None):
        self.counter += 1
        request_id = self.counter
        body = {"jsonrpc": "2.0", "id": request_id, "method": method}
        if params is not None:
            body["params"] = params
        async with self.client.stream("POST", self.url, json=body, headers=self._headers()) as response:
            response.raise_for_status()
            if response.headers.get("mcp-session-id"):
                self.session_id = response.headers["mcp-session-id"]
            content_type = response.headers.get("content-type", "")
            if "application/json" in content_type:
                raw = await response.aread()
                return self._result(json.loads(raw), request_id)
            if "text/event-stream" not in content_type:
                raise ValueError("Unsupported MCP response content type")
            data = []
            async for line in response.aiter_lines():
                if line.startswith("data:"):
                    data.append(line[5:].lstrip(" "))
                elif line == "" and data:
                    payload = "\n".join(data)
                    data = []
                    if not payload:
                        continue
                    message = json.loads(payload)
                    if message.get("id") == request_id:
                        return self._result(message, request_id)
                    if "method" in message and "id" in message:
                        raise ValueError("MCP server-request capability is not supported")
            raise ValueError("MCP stream ended without response")

    async def initialize(self):
        result = await self.rpc("initialize", {"protocolVersion": VERSIONS[0], "capabilities": {},
                                               "clientInfo": {"name": "AgentFlow", "version": "0.1.0"}})
        version = result.get("protocolVersion")
        if version not in VERSIONS:
            raise ValueError("Unsupported negotiated MCP version")
        self.version = version
        await self.notify("notifications/initialized")
        return result

    async def list_tools(self):
        tools = []
        cursor = None
        visited = set()
        while True:
            result = await self.rpc("tools/list", {"cursor": cursor} if cursor else {})
            tools.extend(result.get("tools", []))
            cursor = result.get("nextCursor")
            if not cursor:
                return tools
            if cursor in visited:
                raise ValueError("Repeated MCP pagination cursor")
            visited.add(cursor)

    async def call_tool(self, name, arguments):
        return await self.rpc("tools/call", {"name": name, "arguments": arguments})


class ConnectedTools:
    def __init__(self, local, servers):
        self.local = local
        self.servers = servers
        self.clients = []
        self.remote = {}

    async def initialize(self):
        try:
            for server in self.servers:
                prefix = server["name"]
                if not isinstance(prefix, str) or not prefix.isidentifier() or len(prefix) > 20:
                    raise ValueError("MCP server name must be a short identifier")
                allowed = server.get("allowed_tools", [])
                client = MCPClient(server["url"], server.get("headers"))
                self.clients.append(client)
                await client.initialize()
                for tool in await client.list_tools():
                    if tool["name"] not in allowed:
                        continue
                    name = "mcp_" + prefix + "_" + tool["name"]
                    if len(name) > 64 or name in self.remote:
                        raise ValueError("Invalid or duplicate MCP tool name")
                    schema = {"type": "function", "function": {"name": name,
                              "description": tool.get("description", "Remote MCP tool"),
                              "parameters": tool["inputSchema"]}}
                    self.remote[name] = (client, tool["name"], schema)
        except BaseException:
            await self.close()
            raise

    async def close(self):
        for client in self.clients:
            try:
                await client.close()
            except Exception:
                pass
        self.clients = []
        self.remote = {}

    def schemas(self):
        return self.local.schemas() + [item[2] for item in self.remote.values()]

    def requires_approval(self, name):
        return name not in self.remote and self.local.requires_approval(name)

    async def execute_approved(self, name, arguments):
        if name in self.remote:
            return await self.execute(name, arguments)
        return await self.local.execute_approved(name, arguments)

    async def execute(self, name, arguments):
        if name in self.remote:
            client, remote_name, _ = self.remote[name]
            result = await client.call_tool(remote_name, arguments)
            if result.get("isError"):
                return {"error": True, "content": result.get("content", [])}
            return result.get("structuredContent") or {"content": result.get("content", [])}
        return await self.local.execute(name, arguments)
