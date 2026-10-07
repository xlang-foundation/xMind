"""Durable DAG orchestration over the shared single-agent engine and tool registry."""

import asyncio
import json
from agentflow.engine import Engine


def validate_graph(spec):
    if not isinstance(spec, dict) or not isinstance(spec.get("nodes"), list) or not spec["nodes"]:
        raise ValueError("Graph requires a nonempty nodes list")
    if len(spec["nodes"]) > 100:
        raise ValueError("Graph exceeds 100 nodes")
    nodes = {}
    for node in spec["nodes"]:
        if not isinstance(node, dict) or not isinstance(node.get("id"), str) or not node["id"]:
            raise ValueError("Each node needs a string ID")
        if node["id"] in nodes or node.get("type") not in ("agent", "tool", "human"):
            raise ValueError("Duplicate node or unknown node type")
        deps = node.get("depends_on", [])
        if not isinstance(deps, list) or any(not isinstance(dep, str) for dep in deps):
            raise ValueError("Dependencies must be node IDs")
        if node["type"] == "agent" and not isinstance(node.get("prompt"), str):
            raise ValueError("Agent node requires prompt")
        if node["type"] == "tool" and (not isinstance(node.get("tool"), str) or not isinstance(node.get("arguments", {}), dict)):
            raise ValueError("Tool node requires tool name and object arguments")
        nodes[node["id"]] = node
    visited = set()
    while len(visited) < len(nodes):
        ready = [name for name, node in nodes.items() if name not in visited
                 and all(dep in visited for dep in node.get("depends_on", []))]
        if not ready:
            raise ValueError("Graph contains a cycle or unknown dependency")
        visited.update(ready)
    for node in nodes.values():
        when = node.get("when")
        if when is not None:
            if not isinstance(when, dict) or when.get("node") not in node.get("depends_on", []) or "equals" not in when:
                raise ValueError("Condition must reference a direct dependency with an equals value")
            if not isinstance(when.get("path", []), list):
                raise ValueError("Condition path must be a list")
    return nodes


def resolve(value, states, dependencies):
    if isinstance(value, dict) and set(value) == {"$ref"}:
        ref = value["$ref"]
        if not isinstance(ref, dict) or ref.get("node") not in dependencies:
            raise ValueError("Reference must name a declared dependency")
        result = states[ref["node"]]["result"]
        for key in ref.get("path", []):
            result = result[key]
        return result
    if isinstance(value, dict):
        return {key: resolve(item, states, dependencies) for key, item in value.items()}
    if isinstance(value, list):
        return [resolve(item, states, dependencies) for item in value]
    return value


class GraphEngine:
    def __init__(self, store, provider, tools, permissions=None):
        self.store = store
        self.provider = provider
        self.tools = tools
        self.tasks = {}
        self.permissions = permissions

    def start(self, run_id, resumed=False):
        if run_id in self.tasks:
            raise ValueError("Graph is already executing")
        task = asyncio.create_task(self.execute(run_id, resumed))
        self.tasks[run_id] = task
        task.add_done_callback(lambda completed: self.tasks.pop(run_id, None))

    def cancel(self, run_id):
        if run_id in self.tasks:
            self.tasks[run_id].cancel()

    async def shutdown(self):
        tasks = list(self.tasks.values())
        for task in tasks:
            task.cancel()
        if tasks:
            await asyncio.gather(*tasks, return_exceptions=True)

    def resume(self, run_id, inputs):
        if run_id in self.tasks:
            raise ValueError("Graph is already executing")
        self.store.resume_graph(run_id, inputs)
        self.start(run_id, resumed=True)

    async def node(self, run_id, node, states):
        name = node["id"]
        self.store.checkpoint(run_id, name, "running")
        try:
            deps = node.get("depends_on", [])
            if node["type"] == "tool":
                arguments = resolve(node.get("arguments", {}), states, deps)
                if self.permissions is not None:
                    result = await self.permissions.execute(run_id, name, self.tools, node["tool"], arguments)
                else:
                    result = await self.tools.execute(node["tool"], arguments)
            else:
                if self.provider is None:
                    raise ValueError("Agent graph node requires a configured provider")
                session = self.store.create_session("Graph " + run_id + " / " + name)
                child = self.store.create_run(session["id"])
                self.store.event(run_id, "graph.agent.started", {"node_id": name, "child_run_id": child["id"]})
                prompt = node["prompt"] + "\n\nDependency outputs:\n" + json.dumps({dep: states[dep]["result"] for dep in deps})
                engine = Engine(self.store, self.provider, self.tools, permissions=self.permissions)
                await engine.execute(child["id"], prompt)
                status = self.store.run(child["id"])["status"]
                if status == "cancelled":
                    raise asyncio.CancelledError()
                if status != "completed":
                    raise RuntimeError("Child agent failed")
                result = {"text": self.store.history(session["id"])[-1]["data"]["content"], "run_id": child["id"]}
            self.store.checkpoint(run_id, name, "completed", result)
        except BaseException:
            # A tool or child agent may already have mutated external state.
            self.store.checkpoint(run_id, name, "uncertain")
            raise

    async def execute(self, run_id, resumed=False):
        try:
            nodes = validate_graph(self.store.graph(run_id))
            if not resumed:
                self.store.transition(run_id, "queued", "running")
            while True:
                states = self.store.graph_nodes(run_id)
                finished = {name for name, state in states.items() if state["status"] in ("completed", "skipped")}
                if len(finished) == len(nodes):
                    self.store.transition(run_id, "running", "completed", {"nodes": len(nodes)})
                    return
                if any(state["status"] == "uncertain" for state in states.values()):
                    raise ValueError("Uncertain node requires reconciliation")
                ready = [node for name, node in nodes.items() if name not in finished
                         and all(dep in finished for dep in node.get("depends_on", []))]
                runnable = []
                waiting = False
                for node in ready:
                    deps = node.get("depends_on", [])
                    when = node.get("when")
                    skip = any(states[dep]["status"] == "skipped" for dep in deps)
                    if when is not None and not skip:
                        value = resolve({"$ref": {"node": when["node"], "path": when.get("path", [])}}, states, deps)
                        skip = value != when["equals"]
                    if skip:
                        self.store.checkpoint(run_id, node["id"], "skipped")
                    elif node["type"] == "human":
                        self.store.checkpoint(run_id, node["id"], "waiting", {"prompt": node.get("prompt", "Continue?")})
                        waiting = True
                    else:
                        runnable.append(node)
                if runnable:
                    outcomes = await asyncio.gather(*(self.node(run_id, node, states) for node in runnable), return_exceptions=True)
                    if any(isinstance(outcome, BaseException) for outcome in outcomes):
                        raise RuntimeError("Graph node failed or was interrupted")
                if waiting:
                    self.store.transition(run_id, "running", "paused")
                    return
        except asyncio.CancelledError:
            current = self.store.run(run_id)["status"]
            if current in ("queued", "running", "paused"):
                self.store.transition(run_id, current, "cancelled")
        except Exception as failure:
            current = self.store.run(run_id)["status"]
            if current in ("queued", "running"):
                self.store.transition(run_id, current, "failed", {"error_type": type(failure).__name__})
