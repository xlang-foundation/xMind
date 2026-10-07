"""Stateless MCP tool server using the Streamable HTTP JSON response mode."""

import json
from urllib.parse import urlparse
from fastapi import Request
from fastapi.responses import JSONResponse, Response

VERSIONS = ("2025-11-25", "2025-06-18", "2025-03-26")


def error(request_id, code, message):
    return {"jsonrpc": "2.0", "id": request_id, "error": {"code": code, "message": message}}


class MCPToolsServer:
    def __init__(self, tools):
        self.tools = tools

    async def dispatch(self, message):
        if not isinstance(message, dict):
            return error(None, -32600, "Expected one JSON-RPC message")
        request_id = message.get("id")
        if message.get("jsonrpc") != "2.0" or not isinstance(message.get("method"), str):
            return error(request_id, -32600, "Invalid request")
        if "id" not in message:
            return None
        if isinstance(request_id, bool) or not isinstance(request_id, (int, str)):
            return error(None, -32600, "Invalid request ID")
        params = message.get("params", {})
        if not isinstance(params, dict):
            return error(request_id, -32602, "Expected object parameters")
        method = message["method"]
        if method == "initialize":
            version = params.get("protocolVersion")
            result = {"protocolVersion": version if version in VERSIONS else VERSIONS[0],
                      "capabilities": {"tools": {"listChanged": False}},
                      "serverInfo": {"name": "AgentFlow", "version": "0.1.0"}}
        elif method == "ping":
            result = {}
        elif method == "tools/list":
            if params.get("cursor"):
                return error(request_id, -32602, "Invalid cursor")
            result = {"tools": [{"name": item["function"]["name"],
                                  "description": item["function"]["description"],
                                  "inputSchema": item["function"]["parameters"]}
                                 for item in self.tools.schemas()]}
        elif method == "tools/call":
            name = params.get("name")
            if name not in [item["function"]["name"] for item in self.tools.schemas()]:
                return error(request_id, -32602, "Unknown tool")
            try:
                data = await self.tools.execute(name, params.get("arguments", {}))
                result = {"content": [{"type": "text", "text": json.dumps(data)}],
                          "structuredContent": data, "isError": False}
            except (ValueError, PermissionError, OSError) as failure:
                result = {"content": [{"type": "text", "text": str(failure)}], "isError": True}
        else:
            return error(request_id, -32601, "Method not found")
        return {"jsonrpc": "2.0", "id": request_id, "result": result}

    async def http(self, request: Request):
        origin = request.headers.get("origin")
        if origin:
            parsed = urlparse(origin)
            if parsed.scheme not in ("http", "https") or parsed.hostname not in ("localhost", "127.0.0.1", "::1"):
                return Response(status_code=403)
        if request.method != "POST":
            return Response(status_code=405, headers={"Allow": "POST"})
        version = request.headers.get("mcp-protocol-version", "2025-03-26")
        if version not in VERSIONS:
            return Response(status_code=400)
        accept = request.headers.get("accept", "")
        if "application/json" not in accept or "text/event-stream" not in accept:
            return Response(status_code=406)
        if "application/json" not in request.headers.get("content-type", ""):
            return Response(status_code=415)
        try:
            message = await request.json()
        except ValueError:
            return JSONResponse(error(None, -32700, "Parse error"), status_code=400)
        result = await self.dispatch(message)
        return Response(status_code=202) if result is None else JSONResponse(result)
