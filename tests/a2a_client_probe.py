"""HTTP test-double checks of A2A client JSON-RPC and SSE handling."""
import asyncio
import json
import httpx
from agentflow.a2a_client import A2AClient


async def main():
    def respond(request):
        body = json.loads(request.content)
        if body["method"] == "message/stream":
            task = {"kind": "task", "id": "remote", "status": {"state": "working"}}
            done = {"kind": "status-update", "taskId": "remote", "contextId": "context",
                    "status": {"state": "completed"}, "final": True}
            text = "data: " + json.dumps({"jsonrpc": "2.0", "id": body["id"], "result": task}) + "\n\n"
            text += "data: " + json.dumps({"jsonrpc": "2.0", "id": body["id"], "result": done}) + "\n\n"
            return httpx.Response(200, text=text, headers={"Content-Type": "text/event-stream"})
        if body["method"] == "tasks/cancel":
            return httpx.Response(200, json={"jsonrpc": "2.0", "id": body["id"],
                                  "error": {"code": -32002, "message": "Task cannot be canceled"}})
        return httpx.Response(200, json={"jsonrpc": "2.0", "id": body["id"],
                              "result": {"kind": "task", "id": "remote", "status": {"state": "submitted"}}})
    client = A2AClient("http://peer.test/a2a", transport=httpx.MockTransport(respond))
    assert (await client.send("Delegate"))["id"] == "remote"
    assert (await client.get("remote"))["status"]["state"] == "submitted"
    events = []
    async for event in client.stream("Stream task"):
        events.append(event)
    assert len(events) == 2 and events[-1]["final"]
    try:
        await client.cancel("remote")
        raise AssertionError("A2A error was not surfaced")
    except ValueError as error:
        assert "-32002" in str(error)
    await client.close()
    print("A2A client request/response, SSE and error handling passed against HTTP test double")


asyncio.run(main())
