"""Exercise shared client state, cancellation, missing IDs and restart recovery."""

import os
import tempfile
from fastapi.testclient import TestClient
from agentflow.server import create_app

with tempfile.TemporaryDirectory() as root:
    database = os.path.join(root, "state.sqlite")
    with TestClient(create_app(database)) as client:
        session = client.post("/v1/sessions", json={"title": "Shared coding task"}).json()
        assert client.get("/v1/sessions").json()[0] == session
        response = client.post("/v1/runs", json={"session_id": session["id"]})
        assert response.status_code == 201
        run = response.json()
        assert client.post("/v1/runs", json={"session_id": session["id"]}).status_code == 409
        assert client.post("/v1/runs", json={"session_id": "missing"}).status_code == 404
        cursor = client.get("/v1/runs/" + run["id"] + "/events").json()[-1]["seq"]
        assert client.post("/v1/runs/" + run["id"] + "/cancel").json()["status"] == "cancelled"
        assert len(client.get("/v1/runs/" + run["id"] + "/events", params={"after": cursor}).json()) == 1
        streamed = client.get("/v1/runs/" + run["id"] + "/stream", params={"after": cursor})
        assert streamed.status_code == 200
        assert '"kind": "run.cancelled"' in streamed.text
        assert '"kind": "run.queued"' not in streamed.text
        interrupted = client.post("/v1/runs", json={"session_id": session["id"]}).json()
    with TestClient(create_app(database)) as client:
        assert client.get("/v1/runs/" + interrupted["id"]).json()["status"] == "failed"
        assert client.get("/v1/sessions").json()[0] == session
        assert client.get("/v1/runs/missing").status_code == 404
print("shared API state, cancellation, conflict and restart recovery passed")
