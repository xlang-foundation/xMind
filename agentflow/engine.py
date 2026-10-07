"""General agent execution; graph nodes will use the same engine."""

import asyncio
import json


class Engine:
    def __init__(self, store, provider, tools, max_steps=16, step_timeout=120, permissions=None):
        self.store = store
        self.provider = provider
        self.tools = tools
        self.max_steps = max_steps
        self.step_timeout = step_timeout
        self.tasks = {}
        self.permissions = permissions

    def start(self, run_id, prompt):
        task = asyncio.create_task(self.execute(run_id, prompt))
        self.tasks[run_id] = task
        task.add_done_callback(lambda completed: self.tasks.pop(run_id, None))

    async def shutdown(self):
        tasks = list(self.tasks.values())
        for task in tasks:
            task.cancel()
        if tasks:
            await asyncio.gather(*tasks, return_exceptions=True)

    def cancel(self, run_id):
        task = self.tasks.get(run_id)
        if task is not None:
            task.cancel()

    async def execute(self, run_id, prompt):
        session_id = self.store.run(run_id)["session_id"]
        try:
            self.store.transition(run_id, "queued", "running")
            self.store.message(session_id, "user", {"content": prompt})
            messages = [{"role": item["role"], **item["data"]}
                        for item in self.store.history(session_id)]
            for step in range(self.max_steps):
                self.store.event(run_id, "model.started", {"step": step})
                text = ""
                calls = {}
                async with asyncio.timeout(self.step_timeout):
                    async for event in self.provider.stream(messages, self.tools.schemas()):
                        if event["kind"] == "text":
                            text += event["text"]
                            self.store.event(run_id, "message.delta", {"text": event["text"]})
                        elif event["kind"] == "usage":
                            self.store.event(run_id, "model.usage", event["data"])
                        elif event["kind"] == "tool_delta":
                            delta = event["data"]
                            call = calls.setdefault(delta["index"], {"id": "", "type": "function",
                                                   "function": {"name": "", "arguments": ""}})
                            if delta.get("id"):
                                call["id"] = delta["id"]
                            function = delta.get("function", {})
                            call["function"]["name"] += function.get("name", "")
                            call["function"]["arguments"] += function.get("arguments", "")
                assistant = {"content": text or None}
                if calls:
                    assistant["tool_calls"] = [calls[index] for index in sorted(calls)]
                self.store.message(session_id, "assistant", assistant)
                messages.append({"role": "assistant", **assistant})
                if not calls:
                    self.store.transition(run_id, "running", "completed", {"text": text})
                    return
                for call in assistant["tool_calls"]:
                    name = call["function"]["name"]
                    self.store.event(run_id, "tool.started", {"call_id": call["id"], "name": name})
                    try:
                        arguments = json.loads(call["function"]["arguments"])
                        if self.permissions is not None:
                            result = await self.permissions.execute(run_id, call["id"], self.tools, name, arguments, self.step_timeout)
                        else:
                            async with asyncio.timeout(self.step_timeout):
                                result = await self.tools.execute(name, arguments)
                    except (ValueError, PermissionError, OSError) as error:
                        result = {"error": str(error), "type": type(error).__name__}
                    self.store.event(run_id, "tool.completed", {"call_id": call["id"], "result": result})
                    message = {"tool_call_id": call["id"], "content": json.dumps(result)}
                    self.store.message(session_id, "tool", message)
                    messages.append({"role": "tool", **message})
            raise RuntimeError("Agent step limit exceeded")
        except asyncio.CancelledError:
            current = self.store.run(run_id)["status"]
            if current in ("queued", "running"):
                self.store.transition(run_id, current, "cancelled")
        except Exception as error:
            current = self.store.run(run_id)["status"]
            if current in ("queued", "running"):
                # Do not persist provider response bodies or authentication headers.
                self.store.transition(run_id, current, "failed", {"error_type": type(error).__name__})
