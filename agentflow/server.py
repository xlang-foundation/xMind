"""Versioned shared API. Execution endpoints are added with the agent engine."""

from contextlib import asynccontextmanager
import asyncio
import json
from fastapi import FastAPI, HTTPException, Request
from fastapi.responses import StreamingResponse
from pydantic import BaseModel
from agentflow.store import Store, RunConflict
from agentflow.engine import Engine
from agentflow.tools import WorkspaceTools
from agentflow.mcp import MCPToolsServer
from agentflow.mcp_client import ConnectedTools
from agentflow.graph import GraphEngine, validate_graph
from agentflow.a2a import A2AServer
from agentflow.permissions import PermissionGate


class SessionInput(BaseModel):
    title: str = "New session"


class RunInput(BaseModel):
    session_id: str
    prompt: str | None = None


class GraphInput(BaseModel):
    session_id: str
    spec: dict


class GraphResumeInput(BaseModel):
    inputs: dict


class ApprovalInput(BaseModel):
    decision: str


def create_app(database, provider=None, workspace=None, allow_write=False, mcp_servers=None, ask_write=False):
    store = Store(database)
    tools = ConnectedTools(WorkspaceTools(workspace, allow_write), mcp_servers or []) if workspace else None
    permissions = PermissionGate(store, enabled=ask_write)
    engine = Engine(store, provider, tools, permissions=permissions) if provider and tools else None
    graph_engine = GraphEngine(store, provider, tools, permissions=permissions) if tools else None

    @asynccontextmanager
    async def lifespan(app):
        store.recover_interrupted()
        try:
            if tools:
                await tools.initialize()
            yield
        finally:
            if graph_engine:
                await graph_engine.shutdown()
            if engine:
                await engine.shutdown()
            if tools:
                await tools.close()
            store.close()

    app = FastAPI(title="AgentFlow", version="0.1.0", lifespan=lifespan)
    a2a = A2AServer(store, engine)

    @app.get("/.well-known/agent-card.json")
    async def agent_card(request: Request):
        return a2a.card(str(request.base_url).rstrip("/") + "/a2a")

    @app.post("/a2a")
    async def a2a_endpoint(request: Request):
        return await a2a.http(request)
    if workspace:
        mcp = MCPToolsServer(WorkspaceTools(workspace, allow_write))

        @app.api_route("/mcp", methods=["POST", "GET", "DELETE"])
        async def mcp_endpoint(request: Request):
            return await mcp.http(request)

    @app.get("/v1/health")
    async def health():
        return {"status": "ok", "api_version": "v1"}

    @app.get("/v1/sessions")
    async def sessions():
        return store.sessions()

    @app.post("/v1/sessions", status_code=201)
    async def create_session(body: SessionInput):
        return store.create_session(body.title)

    @app.get("/v1/sessions/{session_id}/history")
    async def history(session_id: str):
        try:
            store.session(session_id)
            return store.history(session_id)
        except KeyError:
            raise HTTPException(404, "Session not found")

    @app.get("/v1/sessions/{session_id}/runs")
    async def session_runs(session_id: str):
        try:
            return store.runs(session_id)
        except KeyError:
            raise HTTPException(404, "Session not found")

    @app.post("/v1/runs", status_code=201)
    async def create_run(body: RunInput):
        if body.prompt is not None and not body.prompt.strip():
            raise HTTPException(400, "Prompt must not be empty")
        if body.prompt is not None and engine is None:
            raise HTTPException(503, "Configure model provider and workspace before execution")
        try:
            run = store.create_run(body.session_id)
            if body.prompt is not None:
                engine.start(run["id"], body.prompt)
            return run
        except KeyError:
            raise HTTPException(404, "Session not found")
        except RunConflict as error:
            raise HTTPException(409, str(error))

    @app.get("/v1/runs/{run_id}")
    async def get_run(run_id: str):
        try:
            return store.run(run_id)
        except KeyError:
            raise HTTPException(404, "Run not found")

    @app.post("/v1/graphs/runs", status_code=201)
    async def create_graph(body: GraphInput):
        if graph_engine is None:
            raise HTTPException(503, "Configure a workspace before graph execution")
        try:
            nodes = validate_graph(body.spec)
            if provider is None and any(node["type"] == "agent" for node in nodes.values()):
                raise HTTPException(503, "Configure a provider for agent graph nodes")
            run = store.create_run(body.session_id, graph=body.spec)
            graph_engine.start(run["id"])
            return run
        except KeyError:
            raise HTTPException(404, "Session not found")
        except RunConflict as failure:
            raise HTTPException(409, str(failure))
        except ValueError as failure:
            raise HTTPException(400, str(failure))

    @app.get("/v1/graphs/runs/{run_id}")
    async def get_graph(run_id: str):
        try:
            return {"run": store.run(run_id), "spec": store.graph(run_id), "nodes": store.graph_nodes(run_id)}
        except KeyError:
            raise HTTPException(404, "Graph run not found")

    @app.post("/v1/graphs/runs/{run_id}/resume")
    async def resume_graph(run_id: str, body: GraphResumeInput):
        if graph_engine is None:
            raise HTTPException(503, "Configure a workspace before graph execution")
        try:
            graph_engine.resume(run_id, body.inputs)
            return store.run(run_id)
        except KeyError:
            raise HTTPException(404, "Graph run not found")
        except RunConflict as failure:
            raise HTTPException(409, str(failure))
        except ValueError as failure:
            raise HTTPException(400, str(failure))

    @app.get("/v1/runs/{run_id}/events")
    async def events(run_id: str, after: int = 0):
        if after < 0:
            raise HTTPException(400, "Cursor must be nonnegative")
        try:
            return store.events(run_id, after)
        except KeyError:
            raise HTTPException(404, "Run not found")

    @app.get("/v1/runs/{run_id}/approvals")
    async def approvals(run_id: str):
        try:
            return store.pending_approvals(run_id)
        except KeyError:
            raise HTTPException(404, "Run not found")

    @app.post("/v1/approvals/{approval_id}")
    async def decide(approval_id: str, body: ApprovalInput):
        if body.decision not in ("allow", "deny"):
            raise HTTPException(400, "Decision must be allow or deny")
        try:
            return store.decide_approval(approval_id, body.decision)
        except KeyError:
            raise HTTPException(404, "Approval not found")
        except RunConflict as error:
            raise HTTPException(409, str(error))

    @app.post("/v1/runs/{run_id}/cancel")
    async def cancel(run_id: str):
        try:
            run = store.run(run_id)
            if run["status"] not in ("queued", "running", "paused"):
                raise HTTPException(409, "Run is already terminal")
            if engine:
                engine.cancel(run_id)
            if graph_engine:
                graph_engine.cancel(run_id)
            return store.transition(run_id, run["status"], "cancelled")
        except KeyError:
            raise HTTPException(404, "Run not found")

    @app.get("/v1/runs/{run_id}/stream")
    async def stream(run_id: str, after: int = 0):
        if after < 0:
            raise HTTPException(400, "Cursor must be nonnegative")
        try:
            store.run(run_id)
        except KeyError:
            raise HTTPException(404, "Run not found")

        async def generate():
            cursor = after
            while True:
                for event in store.events(run_id, cursor):
                    cursor = event["seq"]
                    yield "id: " + str(cursor) + "\ndata: " + json.dumps(event) + "\n\n"
                if store.run(run_id)["status"] in ("completed", "failed", "cancelled", "paused"):
                    return
                await asyncio.sleep(0.1)

        return StreamingResponse(generate(), media_type="text/event-stream",
                                 headers={"Cache-Control": "no-cache"})

    return app
