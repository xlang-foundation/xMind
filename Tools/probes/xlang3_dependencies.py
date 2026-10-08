"""Run with xlang3 to establish application dependency compatibility."""

import asyncio
import json
import sqlite3
import sys

print("runtime", sys.version)
assert json.loads(json.dumps({"message": "AgentFlow", "items": [1, 2]}))["items"] == [1, 2]
db = sqlite3.connect(":memory:")
db.execute("CREATE TABLE probe (value TEXT)")
db.execute("INSERT INTO probe VALUES (?)", ("persistent state",))
assert db.execute("SELECT value FROM probe").fetchone()[0] == "persistent state"
db.close()
print("json/sqlite passed")

async def probe():
    await asyncio.sleep(0)
    return "async passed"

print(asyncio.run(probe()))
from fastapi import FastAPI
from fastapi.testclient import TestClient
import uvicorn

app = FastAPI()

@app.get("/health")
def health():
    return {"status": "ok"}

with TestClient(app) as client:
    response = client.get("/health")
    assert response.status_code == 200
    assert response.json() == {"status": "ok"}
print("fastapi request passed", uvicorn.__version__)
