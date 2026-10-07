"""Exercise real restart persistence and transactional run lifecycle."""

import os
import tempfile
from agentflow.store import Store, RunConflict

with tempfile.TemporaryDirectory() as root:
    path = os.path.join(root, "state.sqlite")
    store = Store(path)
    session = store.create_session("Coding task")
    run = store.create_run(session["id"])
    try:
        store.create_run(session["id"])
        raise AssertionError("Concurrent active run was accepted")
    except RunConflict:
        pass
    store.transition(run["id"], "queued", "running")
    store.message(session["id"], "user", {"content": "Fix the bug"})
    cursor = store.events(run["id"])[-1]["seq"]
    store.close()
    store = Store(path)
    assert store.history(session["id"])[0]["data"]["content"] == "Fix the bug"
    assert store.recover_interrupted() == 1
    events = store.events(run["id"], cursor)
    assert len(events) == 1
    assert events[0]["data"]["reason"] == "server_restart"
    assert store.recover_interrupted() == 0
    next_run = store.create_run(session["id"])
    store.transition(next_run["id"], "queued", "running")
    store.transition(next_run["id"], "running", "completed")
    try:
        store.transition(next_run["id"], "running", "failed")
        raise AssertionError("Terminal run changed")
    except ValueError:
        pass
    store.close()
print("session persistence, event cursors, concurrency and restart recovery passed")
