"""Parallel graph work, restart-safe human pause, agent reuse and uncertain recovery."""

import asyncio
import tempfile
from pathlib import Path
from agentflow.graph import GraphEngine, validate_graph
from agentflow.store import Store, RunConflict
from agentflow.tools import WorkspaceTools


class CountingTools(WorkspaceTools):
    def __init__(self, root):
        super().__init__(root, allow_write=True)
        self.calls = []
        self.active = 0
        self.peak = 0

    async def execute(self, name, arguments):
        self.calls.append((name, arguments["path"]))
        self.active += 1
        self.peak = max(self.peak, self.active)
        try:
            await asyncio.sleep(0.01)
            return await super().execute(name, arguments)
        finally:
            self.active -= 1


class ReviewProvider:
    async def stream(self, messages, tools):
        assert '"after": "source data"' in messages[-1]["content"]
        yield {"kind": "text", "text": "Reviewed saved file"}


async def main():
    for bad in [{"nodes": [{"id": "cycle", "type": "human", "depends_on": ["cycle"]}]},
                {"nodes": [{"id": "missing", "type": "human", "depends_on": ["unknown"]}]}]:
        try:
            validate_graph(bad)
            raise AssertionError("Invalid graph accepted")
        except ValueError:
            pass
    with tempfile.TemporaryDirectory() as root:
        Path(root, "source.txt").write_text("source data", encoding="utf-8")
        database = Path(root, "graph.sqlite")
        store = Store(database)
        tools = CountingTools(root)
        spec = {"nodes": [
            {"id": "read", "type": "tool", "tool": "read_file", "arguments": {"path": "source.txt"}},
            {"id": "inspect", "type": "tool", "tool": "read_file", "arguments": {"path": "source.txt"}},
            {"id": "approval", "type": "human", "prompt": "Copy source?", "depends_on": ["read", "inspect"]},
            {"id": "write", "type": "tool", "tool": "write_file", "depends_on": ["read", "approval"],
             "when": {"node": "approval", "path": ["approved"], "equals": True},
             "arguments": {"path": "copied.txt", "content": {"$ref": {"node": "read", "path": ["content"]}}}},
            {"id": "review", "type": "agent", "prompt": "Review saved content", "depends_on": ["write"]},
        ]}
        session = store.create_session("Graph coding workflow")
        run = store.create_run(session["id"], graph=spec)
        graph = GraphEngine(store, ReviewProvider(), tools)
        await graph.execute(run["id"])
        assert store.run(run["id"])["status"] == "paused"
        assert tools.peak == 2
        assert not Path(root, "copied.txt").exists()
        store.close()
        store = Store(database)
        assert store.recover_interrupted() == 0
        graph = GraphEngine(store, ReviewProvider(), tools)
        graph.resume(run["id"], {"approval": {"approved": True}})
        await asyncio.gather(*list(graph.tasks.values()))
        assert store.run(run["id"])["status"] == "completed"
        assert Path(root, "copied.txt").read_text(encoding="utf-8") == "source data"
        assert len([call for call in tools.calls if call[0] == "read_file"]) == 2
        states = store.graph_nodes(run["id"])
        assert states["review"]["result"]["text"] == "Reviewed saved file"
        child_id = states["review"]["result"]["run_id"]
        assert store.run(child_id)["status"] == "completed"

        declined = store.create_run(session["id"], graph=spec)
        await graph.execute(declined["id"])
        graph.resume(declined["id"], {"approval": {"approved": False}})
        await asyncio.gather(*list(graph.tasks.values()))
        assert store.graph_nodes(declined["id"])["write"]["status"] == "skipped"
        assert store.graph_nodes(declined["id"])["review"]["status"] == "skipped"

        interrupted = store.create_run(session["id"], graph=spec)
        store.transition(interrupted["id"], "queued", "running")
        store.checkpoint(interrupted["id"], "write", "running")
        store.close()
        store = Store(database)
        assert store.recover_interrupted() == 1
        assert store.graph_nodes(interrupted["id"])["write"]["status"] == "uncertain"
        try:
            GraphEngine(store, ReviewProvider(), tools).resume(interrupted["id"], {})
            raise AssertionError("Uncertain side effect was replayed")
        except RunConflict:
            pass
        store.close()
    print("parallel graph, human pause/restart/resume, agent reuse, branching and uncertain recovery passed")


asyncio.run(main())
