"""A2A 0.3.0 JSON-RPC adapter over the shared durable run engine."""

import asyncio
import json
from fastapi import Request
from fastapi.responses import JSONResponse, StreamingResponse
from agentflow.store import RunConflict

STATES = {"queued": "submitted", "running": "working", "paused": "input-required",
          "completed": "completed", "failed": "failed", "cancelled": "canceled"}
TERMINAL = ("completed", "failed", "canceled", "rejected")


class A2AError(ValueError):
    def __init__(self, code, message):
        super().__init__(message)
        self.code = code


def success(request_id, result):
    return {"jsonrpc": "2.0", "id": request_id, "result": result}


def failure(request_id, code, message):
    return {"jsonrpc": "2.0", "id": request_id, "error": {"code": code, "message": message}}


class A2AServer:
    def __init__(self, store, engine):
        self.store = store
        self.engine = engine

    def card(self, url):
        return {"name": "AgentFlow", "description": "xlang3 general and coding agent",
                "url": url, "version": "0.1.0", "protocolVersion": "0.3.0",
                "preferredTransport": "JSONRPC", "capabilities": {"streaming": True, "pushNotifications": False},
                "defaultInputModes": ["text/plain"], "defaultOutputModes": ["text/plain"],
                "skills": [{"id": "agentflow", "name": "AgentFlow agent", "description": "Execute a task using configured workspace tools",
                            "tags": ["agent", "coding"]}]}

    def task(self, task_id, history_length=None):
        message = self.store.a2a_message(task_id)
        run = self.store.run(task_id)
        task = {"kind": "task", "id": task_id, "contextId": run["session_id"],
                "status": {"state": STATES[run["status"]]}, "history": [message]}
        for event in self.store.events(task_id):
            if event["kind"] == "run.completed":
                text = event["data"].get("text", "")
                part = {"kind": "text", "text": text}
                task["artifacts"] = [{"artifactId": task_id + "-output", "parts": [part]}]
                task["history"].append({"kind": "message", "role": "agent", "parts": [part],
                                        "messageId": task_id + "-response", "taskId": task_id,
                                        "contextId": run["session_id"]})
        if history_length is not None:
            if isinstance(history_length, bool) or not isinstance(history_length, int) or history_length < 0:
                raise A2AError(-32602, "historyLength must be nonnegative")
            task["history"] = task["history"][-history_length:] if history_length else []
        return task

    def send(self, params):
        message = params.get("message")
        if not isinstance(message, dict) or message.get("role") != "user" or message.get("kind") != "message":
            raise A2AError(-32602, "Expected user Message")
        message_id = message.get("messageId")
        if not isinstance(message_id, str) or not message_id:
            raise A2AError(-32602, "messageId is required")
        parts = message.get("parts")
        if not isinstance(parts, list) or not parts:
            raise A2AError(-32602, "Message parts are required")
        texts = []
        for part in parts:
            if not isinstance(part, dict) or part.get("kind") != "text" or not isinstance(part.get("text"), str):
                raise A2AError(-32005, "Only text parts are supported")
            texts.append(part["text"])
        prompt = "\n".join(texts)
        if not prompt.strip():
            raise A2AError(-32602, "Message text must not be empty")
        configuration = params.get("configuration", {})
        if not isinstance(configuration, dict):
            raise A2AError(-32602, "Expected object configuration")
        if "blocking" in configuration and not isinstance(configuration["blocking"], bool):
            raise A2AError(-32602, "blocking must be boolean")
        history_length = configuration.get("historyLength")
        if history_length is not None and (isinstance(history_length, bool) or not isinstance(history_length, int) or history_length < 0):
            raise A2AError(-32602, "historyLength must be nonnegative")
        if configuration.get("pushNotificationConfig"):
            raise A2AError(-32003, "Push notification is not supported")
        modes = configuration.get("acceptedOutputModes", ["text/plain"])
        if not isinstance(modes, list) or (modes and "text/plain" not in modes):
            raise A2AError(-32005, "Only text/plain output is supported")
        if message.get("taskId"):
            existing = self.task(message["taskId"])
            if existing["status"]["state"] in TERMINAL:
                raise A2AError(-32004, "Terminal task cannot be restarted")
            raise A2AError(-32004, "Additional input for an active task is not supported yet")
        previous = self.store.a2a_message_run(message_id)
        if previous is not None:
            original = self.store.a2a_message(previous)
            if original["parts"] != parts or (message.get("contextId") is not None and message["contextId"] != original["contextId"]):
                raise A2AError(-32602, "messageId was already used with different content")
            return previous
        if self.engine is None:
            raise A2AError(-32004, "Configure model provider and workspace before execution")
        context = message.get("contextId")
        if context is not None:
            if not isinstance(context, str):
                raise A2AError(-32602, "contextId must be a string")
            self.store.session(context)
        else:
            context = self.store.create_session(prompt[:80])["id"]
        stored = dict(message)
        stored["contextId"] = context
        run = self.store.create_run(context, a2a_message=stored)
        self.engine.start(run["id"], prompt)
        return run["id"]

    async def stream(self, task_id, request_id, after=0):
        task = self.task(task_id)
        context = task["contextId"]
        yield "data: " + json.dumps(success(request_id, task)) + "\n\n"
        if task["status"]["state"] in TERMINAL:
            return
        cursor = after
        artifact_started = False
        while True:
            for event in self.store.events(task_id, cursor):
                cursor = event["seq"]
                kind = event["kind"]
                if kind == "message.delta":
                    result = {"kind": "artifact-update", "taskId": task_id, "contextId": context,
                              "artifact": {"artifactId": task_id + "-output", "parts": [{"kind": "text", "text": event["data"]["text"]}]},
                              "append": artifact_started, "lastChunk": False}
                    artifact_started = True
                elif kind.startswith("run."):
                    status = STATES.get(kind[4:])
                    if status is None:
                        continue
                    result = {"kind": "status-update", "taskId": task_id, "contextId": context,
                              "status": {"state": status}, "final": status in TERMINAL or status == "input-required"}
                    if status == "completed":
                        final_text = "" if artifact_started else event["data"].get("text", "")
                        artifact = {"kind": "artifact-update", "taskId": task_id, "contextId": context,
                                    "artifact": {"artifactId": task_id + "-output", "parts": [{"kind": "text", "text": final_text}]},
                                    "append": artifact_started, "lastChunk": True}
                        yield "data: " + json.dumps(success(request_id, artifact)) + "\n\n"
                else:
                    continue
                yield "id: " + str(cursor) + "\ndata: " + json.dumps(success(request_id, result)) + "\n\n"
                if result.get("final"):
                    return
            await asyncio.sleep(0.1)

    async def http(self, request: Request):
        try:
            body = await request.json()
        except ValueError:
            return JSONResponse(failure(None, -32700, "Invalid JSON"))
        if not isinstance(body, dict) or body.get("jsonrpc") != "2.0" or not isinstance(body.get("method"), str):
            return JSONResponse(failure(None, -32600, "Invalid JSON-RPC request"))
        request_id = body.get("id")
        if isinstance(request_id, bool) or not isinstance(request_id, (str, int)):
            return JSONResponse(failure(None, -32600, "Request ID is required"))
        params = body.get("params", {})
        if not isinstance(params, dict):
            return JSONResponse(failure(request_id, -32602, "Expected object params"))
        try:
            method = body["method"]
            if method in ("message/send", "message/stream"):
                task_id = self.send(params)
                if method == "message/stream":
                    return StreamingResponse(self.stream(task_id, request_id), media_type="text/event-stream")
                if params.get("configuration", {}).get("blocking", False):
                    while self.store.run(task_id)["status"] in ("queued", "running"):
                        await asyncio.sleep(0.1)
                result = self.task(task_id, params.get("configuration", {}).get("historyLength"))
            elif method in ("tasks/get", "tasks/cancel", "tasks/resubscribe"):
                task_id = params.get("id")
                if not isinstance(task_id, str):
                    raise A2AError(-32602, "Task ID is required")
                result = self.task(task_id, params.get("historyLength"))
                if method == "tasks/cancel":
                    run = self.store.run(task_id)
                    if run["status"] not in ("queued", "running", "paused"):
                        raise A2AError(-32002, "Task cannot be canceled")
                    if self.engine:
                        self.engine.cancel(task_id)
                    self.store.transition(task_id, run["status"], "cancelled")
                    result = self.task(task_id)
                elif method == "tasks/resubscribe":
                    after = int(request.headers.get("last-event-id", "0"))
                    if after < 0:
                        raise A2AError(-32602, "Invalid event cursor")
                    return StreamingResponse(self.stream(task_id, request_id, after), media_type="text/event-stream")
            elif method.startswith("tasks/pushNotificationConfig/"):
                raise A2AError(-32003, "Push notification is not supported")
            elif method == "agent/getAuthenticatedExtendedCard":
                raise A2AError(-32007, "Authenticated extended card is not configured")
            else:
                raise A2AError(-32601, "Method not found")
            return JSONResponse(success(request_id, result))
        except A2AError as error:
            return JSONResponse(failure(request_id, error.code, str(error)))
        except KeyError:
            return JSONResponse(failure(request_id, -32001, "Task or context not found"))
        except RunConflict:
            return JSONResponse(failure(request_id, -32004, "Context already has an active task"))
        except ValueError:
            return JSONResponse(failure(request_id, -32602, "Invalid parameters"))
        except Exception:
            return JSONResponse(failure(request_id, -32603, "Internal server error"))
