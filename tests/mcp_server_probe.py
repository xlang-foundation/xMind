"""MCP HTTP lifecycle, discovery, calls and protocol error checks."""

import tempfile
from pathlib import Path
from fastapi.testclient import TestClient
from agentflow.server import create_app

with tempfile.TemporaryDirectory() as root:
    Path(root, "sample.txt").write_text("MCP read", encoding="utf-8")
    with TestClient(create_app(Path(root, "state.sqlite"), workspace=root)) as client:
        headers = {"Accept": "application/json, text/event-stream", "MCP-Protocol-Version": "2025-11-25"}
        def rpc(method, params, request_id=1):
            return client.post("/mcp", json={"jsonrpc": "2.0", "id": request_id,
                                             "method": method, "params": params}, headers=headers)
        response = rpc("initialize", {"protocolVersion": "2025-11-25", "capabilities": {},
                                      "clientInfo": {"name": "probe", "version": "1"}})
        assert response.json()["result"]["protocolVersion"] == "2025-11-25"
        assert client.post("/mcp", json={"jsonrpc": "2.0", "method": "notifications/initialized"}, headers=headers).status_code == 202
        assert len(rpc("tools/list", {}).json()["result"]["tools"]) == 5
        result = rpc("tools/call", {"name": "read_file", "arguments": {"path": "sample.txt"}}).json()["result"]
        assert result["structuredContent"]["content"] == "MCP read"
        assert not result["isError"]
        assert rpc("tools/call", {"name": "write_file", "arguments": {"path": "sample.txt", "content": "bad"}}).json()["result"]["isError"]
        assert rpc("tools/call", {"name": "missing"}).json()["error"]["code"] == -32602
        assert rpc("missing", {}).json()["error"]["code"] == -32601
        assert client.get("/mcp").status_code == 405
        assert client.post("/mcp", json={}, headers={**headers, "Origin": "https://evil.example"}).status_code == 403
        assert client.post("/mcp", json={}, headers={**headers, "MCP-Protocol-Version": "bad"}).status_code == 400
print("MCP server lifecycle, discovery, read call, write denial and transport errors passed")
