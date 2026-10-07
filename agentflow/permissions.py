"""Persistent per-operation approval gate shared by agents and graph tools."""
import asyncio


class PermissionGate:
    def __init__(self, store, enabled=False):
        self.store = store
        self.enabled = enabled

    async def execute(self, run_id, call_id, tools, name, arguments, timeout=120):
        if not self.enabled or not tools.requires_approval(name):
            async with asyncio.timeout(timeout):
                return await tools.execute(name, arguments)
        approval_id = self.store.request_approval(run_id, call_id, name, arguments)
        try:
            while True:
                approval = self.store.approval(approval_id)
                decision = approval["decision"]
                if decision is not None:
                    break
                await asyncio.sleep(0.1)
            if decision != "allow":
                raise PermissionError("Operation was not approved")
            async with asyncio.timeout(timeout):
                return await tools.execute_approved(name, arguments)
        except asyncio.CancelledError:
            if self.store.approval(approval_id)["decision"] is None:
                self.store.decide_approval(approval_id, "cancelled")
            raise
