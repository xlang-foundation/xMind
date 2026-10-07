"""MCP pagination, SSE response assembly, session headers and protocol failures."""

import asyncio
import json
import httpx
from agentflow.mcp_client import MCPClient


async def main():
    seen = []
    def respond(request):
        message = json.loads(request.content)
        seen.append(message)
        method = message["method"]
        if method == "initialize":
            result = {"protocolVersion": "2025-11-25", "capabilities": {"tools": {}},
                      "serverInfo": {"name": "peer", "version": "1"}}
            return httpx.Response(200, json={"jsonrpc": "2.0", "id": message["id"], "result": result},
                                  headers={"MCP-Session-Id": "session-test"})
        assert request.headers["MCP-Session-Id"] == "session-test"
        assert request.headers["MCP-Protocol-Version"] == "2025-11-25"
        if method == "notifications/initialized":
            return httpx.Response(202)
        if method == "tools/list":
            result = {"tools": [{"name": "first"}], "nextCursor": "next"} if not message["params"].get("cursor") else {"tools": [{"name": "second"}]}
        elif method == "tools/call":
            result = {"content": [{"type": "text", "text": "answer"}], "isError": False}
        else:
            return httpx.Response(200, json={"jsonrpc": "2.0", "id": 999, "result": {}})
        body = "event: message\ndata: " + json.dumps({"jsonrpc": "2.0", "id": message["id"], "result": result}) + "\n\n"
        return httpx.Response(200, text=body, headers={"Content-Type": "text/event-stream"})

    client = MCPClient("http://peer.test/mcp", transport=httpx.MockTransport(respond))
    await client.initialize()
    assert len(await client.list_tools()) == 2
    assert (await client.call_tool("first", {}))["content"][0]["text"] == "answer"
    try:
        await client.rpc("bad")
        raise AssertionError("Mismatched ID accepted")
    except ValueError:
        pass
    client.session_id = None
    await client.close()
    print("MCP client initialization, pagination, SSE assembly and response validation passed")


asyncio.run(main())
