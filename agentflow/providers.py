"""Provider interface and OpenAI-compatible streaming HTTP adapter."""

import json
import httpx


class ChatProvider:
    def __init__(self, base_url, model, api_key, timeout=60, transport=None):
        self.base_url = base_url.rstrip("/")
        self.model = model
        self.api_key = api_key
        self.timeout = timeout
        self.transport = transport

    async def stream(self, messages, tools):
        payload = {"model": self.model, "messages": messages, "stream": True,
                   "stream_options": {"include_usage": True}}
        if tools:
            payload["tools"] = tools
        headers = {"Authorization": "Bearer " + self.api_key}
        async with httpx.AsyncClient(timeout=self.timeout, transport=self.transport) as client:
            async with client.stream("POST", self.base_url + "/chat/completions",
                                     headers=headers, json=payload) as response:
                response.raise_for_status()
                async for line in response.aiter_lines():
                    if not line.startswith("data:"):
                        continue
                    data = line[5:].strip()
                    if data == "[DONE]":
                        return
                    chunk = json.loads(data)
                    if chunk.get("usage"):
                        yield {"kind": "usage", "data": chunk["usage"]}
                    for choice in chunk.get("choices", []):
                        if choice.get("index", 0) != 0:
                            continue
                        delta = choice.get("delta", {})
                        if delta.get("content"):
                            yield {"kind": "text", "text": delta["content"]}
                        for call in delta.get("tool_calls", []):
                            yield {"kind": "tool_delta", "data": call}


def provider_from_env():
    import os
    model = os.environ.get("AGENTFLOW_MODEL")
    key = os.environ.get("AGENTFLOW_API_KEY")
    if not model or not key:
        return None
    return ChatProvider(os.environ.get("AGENTFLOW_BASE_URL", "https://api.openai.com/v1"), model, key)
