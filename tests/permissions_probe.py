"""One-operation approval, denial, cancellation and durable permission records."""
import asyncio
import tempfile
from pathlib import Path
from agentflow.store import Store, RunConflict
from agentflow.tools import WorkspaceTools
from agentflow.permissions import PermissionGate


async def pending(store, run_id):
    for _ in range(100):
        approvals = store.pending_approvals(run_id)
        if approvals:
            return approvals[0]
        await asyncio.sleep(0.01)
    raise AssertionError("Approval request not created")


async def main():
    with tempfile.TemporaryDirectory() as root:
        store = Store(Path(root, "state.sqlite"))
        session = store.create_session()
        run = store.create_run(session["id"])
        store.transition(run["id"], "queued", "running")
        tools = WorkspaceTools(root)
        gate = PermissionGate(store, enabled=True)
        task = asyncio.create_task(gate.execute(run["id"], "write-1", tools, "write_file", {"path": "new.txt", "content": "approved"}))
        approval = await pending(store, run["id"])
        assert not Path(root, "new.txt").exists()
        assert approval["arguments"]["content"] == "approved"
        store.decide_approval(approval["id"], "allow")
        result = await task
        assert result["after"] == "approved"
        assert not tools.allow_write
        try:
            await tools.execute("write_file", {"path": "new.txt", "content": "unapproved"})
            raise AssertionError("Single approval changed global permissions")
        except PermissionError:
            pass
        try:
            store.decide_approval(approval["id"], "deny")
            raise AssertionError("Resolved approval changed")
        except RunConflict:
            pass

        denied = asyncio.create_task(gate.execute(run["id"], "write-2", tools, "write_file", {"path": "new.txt", "content": "denied"}))
        approval = await pending(store, run["id"])
        store.decide_approval(approval["id"], "deny")
        try:
            await denied
            raise AssertionError("Denied write executed")
        except PermissionError:
            pass
        assert Path(root, "new.txt").read_text(encoding="utf-8") == "approved"

        cancelled = asyncio.create_task(gate.execute(run["id"], "write-3", tools, "write_file", {"path": "new.txt", "content": "cancelled"}))
        approval = await pending(store, run["id"])
        cancelled.cancel()
        try:
            await cancelled
        except asyncio.CancelledError:
            pass
        assert store.approval(approval["id"])["decision"] == "cancelled"
        assert store.pending_approvals(run["id"]) == []
        store.close()
    print("per-operation approval, denial, cancellation and permission isolation passed")


asyncio.run(main())
