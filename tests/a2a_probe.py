"""A2A discovery, request validation and task interoperability over shared runs."""
import asyncio
import json
import tempfile
from pathlib import Path
from fastapi.testclient import TestClient
import httpx
from agentflow.server import create_app
from agentflow.a2a_client import A2AClient


class Provider:
    async def stream(self, messages, tools):
        yield {"kind": "text", "text": "A2A reply"}


with tempfile.TemporaryDirectory() as root:
    with TestClient(create_app(Path(root, "state.sqlite"), Provider(), root)) as client:
        card = client.get("/.well-known/agent-card.json").json()
        assert card["protocolVersion"] == "0.3.0"
        assert card["capabilities"]["streaming"]
        def rpc(method, params):
            return client.post("/a2a", json={"jsonrpc": "2.0", "id": "probe", "method": method, "params": params}).json()
        assert rpc("tasks/get", {"id": "missing"})["error"]["code"] == -32001
        assert rpc("missing", {})["error"]["code"] == -32601
        assert rpc("tasks/pushNotificationConfig/set", {})["error"]["code"] == -32003
        message = {"kind": "message", "role": "user", "messageId": "message-probe",
                   "parts": [{"kind": "text", "text": "Hello"}]}
        invalid = rpc("message/send", {"message": message, "configuration": {"historyLength": -1}})
        assert invalid["error"]["code"] == -32602
        assert client.get("/v1/sessions").json() == []
        task = rpc("message/send", {"message": message, "configuration": {"blocking": True}})
        assert "result" in task, task
        task = task["result"]
        assert task["status"]["state"] == "completed", task
        assert task["artifacts"][0]["parts"][0]["text"] == "A2A reply"
        assert client.get("/v1/runs/" + task["id"]).json()["status"] == "completed"
        assert rpc("message/send", {"message": message})["result"]["id"] == task["id"]
        assert rpc("tasks/get", {"id": task["id"], "historyLength": 0})["result"]["history"] == []
        assert rpc("tasks/cancel", {"id": task["id"]})["error"]["code"] == -32002


async def check_client():
    def respond(request):
        body = json.loads(request.content)
        return httpx.Response(200, json={"jsonrpc": "2.0", "id": body["id"],
                              "result": {"kind": "task", "id": "remote-task", "status": {"state": "submitted"}}})
    client = A2AClient("http://peer.test/a2a", transport=httpx.MockTransport(respond))
    assert (await client.send("Delegate"))["id"] == "remote-task"
    assert (await client.get("remote-task"))["status"]["state"] == "submitted"
    await client.close()


asyncio.run(check_client())
print("A2A task creation, retrieval, shared backend state, deduplication and protocol errors passed")
