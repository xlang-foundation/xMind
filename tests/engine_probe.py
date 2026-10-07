"""HTTP provider/tool integration, permissions, bounded execution and cancellation."""

import asyncio
import json
import os
import tempfile
from pathlib import Path
import httpx
from agentflow.providers import ChatProvider
from agentflow.engine import Engine
from agentflow.store import Store
from agentflow.tools import WorkspaceTools


async def main():
    with tempfile.TemporaryDirectory() as root:
        Path(root, "source.txt").write_text("original", encoding="utf-8")
        requests = []

        def respond(request):
            body = json.loads(request.content)
            requests.append(body)
            if body["messages"][-1]["role"] == "user":
                chunks = [
                    {"choices": [{"delta": {"tool_calls": [{"index": 0, "id": "read-1",
                     "function": {"name": "read_file", "arguments": '{"path":'}}]}}]},
                    {"choices": [{"delta": {"tool_calls": [{"index": 0,
                     "function": {"arguments": '"source.txt"}'}}]}}]},
                ]
            else:
                assert json.loads(body["messages"][-1]["content"])["content"] == "original"
                chunks = [{"choices": [{"delta": {"content": "Read "}}]},
                          {"choices": [{"delta": {"content": "original"}}]},
                          {"choices": [], "usage": {"total_tokens": 20}}]
            text = "".join("data: " + json.dumps(chunk) + "\n\n" for chunk in chunks)
            return httpx.Response(200, text=text + "data: [DONE]\n\n")

        provider = ChatProvider("https://provider.test/v1", "test", "test-key",
                                transport=httpx.MockTransport(respond))
        store = Store(os.path.join(root, "state.sqlite"))
        session = store.create_session()
        run = store.create_run(session["id"])
        tools = WorkspaceTools(root)
        engine = Engine(store, provider, tools)
        await engine.execute(run["id"], "Read source.txt")
        assert store.run(run["id"])["status"] == "completed", store.events(run["id"])
        assert len(requests) == 2
        events = store.events(run["id"])
        assert events[-1]["data"].get("text") == "Read original", events
        assert any(event["kind"] == "model.usage" for event in events)
        assert store.history(session["id"])[-1]["data"]["content"] == "Read original"

        try:
            await tools.execute("write_file", {"path": "source.txt", "content": "changed"})
            raise AssertionError("Write permission bypassed")
        except PermissionError:
            pass
        try:
            await tools.execute("read_file", {"path": "../outside.txt"})
            raise AssertionError("Workspace boundary bypassed")
        except PermissionError:
            pass
        writable = WorkspaceTools(root, allow_write=True)
        result = await writable.execute("write_file", {"path": "source.txt", "content": "changed"})
        assert result["before"] == "original"
        assert Path(root, "source.txt").read_text(encoding="utf-8") == "changed"

        class WaitingProvider:
            async def stream(self, messages, schemas):
                await asyncio.sleep(10)
                yield {"kind": "text", "text": "should not finish"}

        cancel_run = store.create_run(session["id"])
        waiting = Engine(store, WaitingProvider(), tools)
        waiting.start(cancel_run["id"], "Wait")
        await asyncio.sleep(0.01)
        waiting.cancel(cancel_run["id"])
        await waiting.shutdown()
        assert store.run(cancel_run["id"])["status"] == "cancelled"
        timeout_run = store.create_run(session["id"])
        timeout_engine = Engine(store, WaitingProvider(), tools, step_timeout=0.01)
        await timeout_engine.execute(timeout_run["id"], "Timeout")
        assert store.run(timeout_run["id"])["status"] == "failed"
        assert store.events(timeout_run["id"])[-1]["data"]["error_type"] == "TimeoutError"
        store.close()
    print("HTTP streaming, tool round trip, permissions, cancellation and timeout passed")


asyncio.run(main())
