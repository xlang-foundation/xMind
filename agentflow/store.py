"""Transactional session and run state shared by every client."""

import json
import sqlite3
import uuid


class RunConflict(ValueError):
    """A session already has an active run."""


class Store:
    def __init__(self, path):
        self.db = sqlite3.connect(str(path), check_same_thread=False)
        self.db.execute("PRAGMA journal_mode=WAL")
        self.db.execute("PRAGMA foreign_keys=ON")
        self.db.executescript("""
            CREATE TABLE IF NOT EXISTS sessions (
                id TEXT PRIMARY KEY, title TEXT NOT NULL);
            CREATE TABLE IF NOT EXISTS runs (
                id TEXT PRIMARY KEY, session_id TEXT NOT NULL REFERENCES sessions(id),
                status TEXT NOT NULL);
            CREATE UNIQUE INDEX IF NOT EXISTS one_active_run
                ON runs(session_id) WHERE status IN ('queued', 'running');
            CREATE TABLE IF NOT EXISTS events (
                seq INTEGER PRIMARY KEY AUTOINCREMENT,
                run_id TEXT NOT NULL REFERENCES runs(id),
                kind TEXT NOT NULL, data TEXT NOT NULL);
            CREATE TABLE IF NOT EXISTS messages (
                seq INTEGER PRIMARY KEY AUTOINCREMENT,
                session_id TEXT NOT NULL REFERENCES sessions(id),
                role TEXT NOT NULL, data TEXT NOT NULL);
            CREATE TABLE IF NOT EXISTS graph_specs (
                run_id TEXT PRIMARY KEY REFERENCES runs(id), spec TEXT NOT NULL);
            CREATE TABLE IF NOT EXISTS graph_nodes (
                run_id TEXT NOT NULL REFERENCES runs(id), node_id TEXT NOT NULL,
                status TEXT NOT NULL, result TEXT,
                PRIMARY KEY(run_id,node_id));
            CREATE TABLE IF NOT EXISTS a2a_messages (
                run_id TEXT PRIMARY KEY REFERENCES runs(id),
                message_id TEXT NOT NULL UNIQUE, message TEXT NOT NULL);
            CREATE TABLE IF NOT EXISTS approvals (
                id TEXT PRIMARY KEY, run_id TEXT NOT NULL REFERENCES runs(id),
                call_id TEXT NOT NULL, tool TEXT NOT NULL, arguments TEXT NOT NULL,
                decision TEXT, UNIQUE(run_id,call_id));
            DROP INDEX IF EXISTS one_active_run;
            CREATE UNIQUE INDEX one_active_run ON runs(session_id)
                WHERE status IN ('queued','running','paused');
        """)

    def close(self):
        self.db.close()

    def create_session(self, title="New session"):
        session_id = str(uuid.uuid4())
        with self.db:
            self.db.execute("INSERT INTO sessions VALUES (?, ?)", (session_id, title))
        return {"id": session_id, "title": title}

    def sessions(self):
        return [{"id": row[0], "title": row[1]} for row in
                self.db.execute("SELECT id, title FROM sessions ORDER BY rowid")]

    def create_run(self, session_id, graph=None, a2a_message=None):
        run_id = str(uuid.uuid4())
        if self.db.execute("SELECT id FROM sessions WHERE id=?", (session_id,)).fetchone() is None:
            raise KeyError(session_id)
        if self.db.execute("SELECT id FROM runs WHERE session_id=? AND status IN ('queued','running','paused')",
                           (session_id,)).fetchone() is not None:
            raise RunConflict("Session already has an active run")
        try:
            with self.db:
                self.db.execute("INSERT INTO runs VALUES (?, ?, 'queued')", (run_id, session_id))
                self._event(run_id, "run.queued", {})
                if graph is not None:
                    self.db.execute("INSERT INTO graph_specs VALUES (?,?)", (run_id, json.dumps(graph)))
                if a2a_message is not None:
                    self.db.execute("INSERT INTO a2a_messages VALUES (?,?,?)",
                                    (run_id, a2a_message["messageId"], json.dumps(a2a_message)))
        except Exception:
            # Preserve database errors unless another connection won the unique-index race.
            if self.db.execute("SELECT id FROM runs WHERE session_id=? AND status IN ('queued','running','paused')",
                               (session_id,)).fetchone() is not None:
                raise RunConflict("Session already has an active run")
            raise
        return self.run(run_id)

    def run(self, run_id):
        row = self.db.execute("SELECT id, session_id, status FROM runs WHERE id=?", (run_id,)).fetchone()
        if row is None:
            raise KeyError(run_id)
        return {"id": row[0], "session_id": row[1], "status": row[2]}

    def session(self, session_id):
        row = self.db.execute("SELECT id,title FROM sessions WHERE id=?", (session_id,)).fetchone()
        if row is None:
            raise KeyError(session_id)
        return {"id": row[0], "title": row[1]}

    def runs(self, session_id):
        self.session(session_id)
        return [{"id": row[0], "session_id": session_id, "status": row[1]} for row in
                self.db.execute("SELECT id,status FROM runs WHERE session_id=? ORDER BY rowid",
                                (session_id,))]

    def _event(self, run_id, kind, data):
        self.db.execute("INSERT INTO events(run_id,kind,data) VALUES (?,?,?)",
                        (run_id, kind, json.dumps(data)))

    def event(self, run_id, kind, data):
        with self.db:
            self._event(run_id, kind, data)

    def transition(self, run_id, expected, status, data=None):
        allowed = {"queued": ("running", "cancelled", "failed"),
                   "running": ("completed", "failed", "cancelled", "paused"),
                   "paused": ("running", "cancelled")}
        if status not in allowed.get(expected, ()):
            raise ValueError("Invalid run transition")
        with self.db:
            changed = self.db.execute("UPDATE runs SET status=? WHERE id=? AND status=?",
                                      (status, run_id, expected)).rowcount
            if changed != 1:
                raise ValueError("Run state changed or run does not exist; affected rows=" + str(changed))
            self._event(run_id, "run." + status, data or {})
        return self.run(run_id)

    def events(self, run_id, after=0):
        self.run(run_id)
        return [{"seq": row[0], "run_id": run_id, "kind": row[1], "data": json.loads(row[2])}
                for row in self.db.execute(
                    "SELECT seq,kind,data FROM events WHERE run_id=? AND seq>? ORDER BY seq",
                    (run_id, after))]

    def message(self, session_id, role, data):
        with self.db:
            self.db.execute("INSERT INTO messages(session_id,role,data) VALUES (?,?,?)",
                            (session_id, role, json.dumps(data)))

    def history(self, session_id):
        return [{"role": row[0], "data": json.loads(row[1])} for row in
                self.db.execute("SELECT role,data FROM messages WHERE session_id=? ORDER BY seq",
                                (session_id,))]

    def recover_interrupted(self):
        with self.db:
            self.db.execute("UPDATE graph_nodes SET status='uncertain' WHERE status='running'")
            rows = self.db.execute("SELECT id FROM runs WHERE status IN ('queued','running')").fetchall()
            for row in rows:
                self.db.execute("UPDATE runs SET status='failed' WHERE id=?", (row[0],))
                self._event(row[0], "run.failed", {"reason": "server_restart", "resumable": True})
            self.db.execute("UPDATE approvals SET decision='cancelled' WHERE decision IS NULL AND "
                            "run_id IN (SELECT id FROM runs WHERE status IN ('failed','completed','cancelled'))")
        return len(rows)

    def save_graph(self, run_id, spec):
        with self.db:
            self.db.execute("INSERT INTO graph_specs VALUES (?,?)", (run_id, json.dumps(spec)))

    def graph(self, run_id):
        row = self.db.execute("SELECT spec FROM graph_specs WHERE run_id=?", (run_id,)).fetchone()
        if row is None:
            raise KeyError(run_id)
        return json.loads(row[0])

    def graph_nodes(self, run_id):
        self.graph(run_id)
        return {row[0]: {"status": row[1], "result": json.loads(row[2]) if row[2] is not None else None}
                for row in self.db.execute("SELECT node_id,status,result FROM graph_nodes WHERE run_id=?", (run_id,))}

    def checkpoint(self, run_id, node_id, status, result=None):
        with self.db:
            self._checkpoint(run_id, node_id, status, result)

    def _checkpoint(self, run_id, node_id, status, result):
        self.db.execute("INSERT INTO graph_nodes VALUES (?,?,?,?) ON CONFLICT(run_id,node_id) "
                        "DO UPDATE SET status=excluded.status,result=excluded.result",
                        (run_id, node_id, status, json.dumps(result)))
        self._event(run_id, "graph.node." + status, {"node_id": node_id, "result": result})

    def resume_graph(self, run_id, inputs):
        self.graph(run_id)
        with self.db:
            run = self.run(run_id)
            if run["status"] not in ("paused", "failed"):
                raise RunConflict("Graph must be paused or failed to resume")
            if self.db.execute("SELECT id FROM runs WHERE session_id=? AND id<>? AND status IN ('queued','running','paused')",
                               (run["session_id"], run_id)).fetchone() is not None:
                raise RunConflict("Session already has another active run")
            states = self.graph_nodes(run_id)
            if any(state["status"] in ("uncertain", "running") for state in states.values()):
                raise RunConflict("Interrupted node may have side effects; reconciliation is required before resume")
            waiting = {name for name, state in states.items() if state["status"] == "waiting"}
            if set(inputs) != waiting:
                raise ValueError("Provide input for every waiting human node, and no other nodes")
            self.db.execute("UPDATE runs SET status='running' WHERE id=?", (run_id,))
            self._event(run_id, "run.running", {"resumed": True})
            for name, value in inputs.items():
                self._checkpoint(run_id, name, "completed", value)

    def a2a_message(self, run_id):
        row = self.db.execute("SELECT message FROM a2a_messages WHERE run_id=?", (run_id,)).fetchone()
        if row is None:
            raise KeyError(run_id)
        return json.loads(row[0])

    def a2a_message_run(self, message_id):
        row = self.db.execute("SELECT run_id FROM a2a_messages WHERE message_id=?", (message_id,)).fetchone()
        return row[0] if row is not None else None

    def request_approval(self, run_id, call_id, tool, arguments):
        if self.run(run_id)["status"] != "running":
            raise RunConflict("Approval requires a running operation")
        approval_id = str(uuid.uuid4())
        with self.db:
            self.db.execute("INSERT INTO approvals VALUES (?,?,?,?,?,NULL)",
                            (approval_id, run_id, call_id, tool, json.dumps(arguments)))
            self._event(run_id, "permission.requested", {"id": approval_id, "call_id": call_id,
                                                         "tool": tool, "arguments": arguments})
        return approval_id

    def approval(self, approval_id):
        row = self.db.execute("SELECT id,run_id,call_id,tool,arguments,decision FROM approvals WHERE id=?",
                              (approval_id,)).fetchone()
        if row is None:
            raise KeyError(approval_id)
        return {"id": row[0], "run_id": row[1], "call_id": row[2], "tool": row[3],
                "arguments": json.loads(row[4]), "decision": row[5]}

    def pending_approvals(self, run_id):
        self.run(run_id)
        return [self.approval(row[0]) for row in
                self.db.execute("SELECT id FROM approvals WHERE run_id=? AND decision IS NULL ORDER BY rowid", (run_id,))]

    def decide_approval(self, approval_id, decision):
        if decision not in ("allow", "deny", "cancelled"):
            raise ValueError("Invalid approval decision")
        approval = self.approval(approval_id)
        if approval["decision"] is not None:
            raise RunConflict("Approval has already been resolved")
        if decision != "cancelled" and self.run(approval["run_id"])["status"] != "running":
            raise RunConflict("Run is no longer running")
        with self.db:
            changed = self.db.execute("UPDATE approvals SET decision=? WHERE id=? AND decision IS NULL",
                                      (decision, approval_id)).rowcount
            if changed != 1:
                raise RunConflict("Approval state changed")
            self._event(approval["run_id"], "permission.decided", {"id": approval_id, "decision": decision})
        return self.approval(approval_id)
