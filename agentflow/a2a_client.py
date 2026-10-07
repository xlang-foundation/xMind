"""A2A JSON-RPC client over the same HTTP provider transport dependencies."""
import uuid
import json
import httpx


class A2AClient:
    def __init__(self, url, headers=None, transport=None):
        self.url = url
        self.headers = headers or {}
        self.client = httpx.AsyncClient(timeout=120, transport=transport, follow_redirects=False)
        self.counter = 0

    async def close(self):
        await self.client.aclose()

    async def discover(self, card_url):
        response = await self.client.get(card_url, headers=self.headers)
        response.raise_for_status()
        card = response.json()
        if card.get("protocolVersion") != "0.3.0" or card.get("preferredTransport", "JSONRPC") != "JSONRPC":
            raise ValueError("Only A2A 0.3.0 JSON-RPC is supported")
        # Do not redirect credentials or change configured endpoint based on an untrusted card.
        return card

    def result(self, data, request_id):
        if data.get("jsonrpc") != "2.0" or data.get("id") != request_id:
            raise ValueError("A2A response ID/version mismatch")
        if "error" in data:
            raise ValueError("A2A error code " + str(data["error"].get("code")))
        if "result" not in data:
            raise ValueError("A2A result missing")
        return data["result"]

    async def rpc(self, method, params):
        self.counter += 1
        request_id = self.counter
        response = await self.client.post(self.url, headers=self.headers,
                                         json={"jsonrpc": "2.0", "id": request_id, "method": method, "params": params})
        response.raise_for_status()
        return self.result(response.json(), request_id)

    def message_params(self, text, context_id=None, message_id=None, blocking=False):
        message = {"kind": "message", "role": "user", "messageId": message_id or str(uuid.uuid4()),
                   "parts": [{"kind": "text", "text": text}]}
        if context_id is not None:
            message["contextId"] = context_id
        return {"message": message, "configuration": {"blocking": blocking, "acceptedOutputModes": ["text/plain"]}}

    async def send(self, text, context_id=None, message_id=None, blocking=False):
        return await self.rpc("message/send", self.message_params(text, context_id, message_id, blocking))

    async def get(self, task_id):
        return await self.rpc("tasks/get", {"id": task_id})

    async def cancel(self, task_id):
        return await self.rpc("tasks/cancel", {"id": task_id})

    async def stream(self, text):
        self.counter += 1
        request_id = self.counter
        async with self.client.stream("POST", self.url, headers={**self.headers, "Accept": "text/event-stream"},
                json={"jsonrpc": "2.0", "id": request_id, "method": "message/stream", "params": self.message_params(text)}) as response:
            response.raise_for_status()
            data = []
            async for line in response.aiter_lines():
                if line.startswith("data:"):
                    data.append(line[5:].lstrip(" "))
                elif not line and data:
                    yield self.result(json.loads("\n".join(data)), request_id)
                    data = []
