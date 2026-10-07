"""Mix discarded SELECT cursors and temporary UPDATE rowcount property access."""

import sqlite3


class State:
    def __init__(self):
        self.db = sqlite3.connect(":memory:")
        self.db.execute("CREATE TABLE runs(id TEXT PRIMARY KEY,status TEXT)")
        self.db.execute("INSERT INTO runs VALUES ('first','queued')")

    def transition(self, before, after):
        with self.db:
            count = self.db.execute("UPDATE runs SET status=? WHERE id=? AND status=?", (after, "first", before)).rowcount
            actual = self.db.execute("SELECT status FROM runs WHERE id=?", ("first",)).fetchone()
            assert count == 1, (count, before, after, actual)


state = State()
for index in range(1000):
    state.db.execute("SELECT id FROM runs WHERE id=?", ("first",)).fetchone()
    state.transition("queued", "running")
    state.db.execute("SELECT status FROM runs WHERE id=?", ("first",)).fetchone()
    state.transition("running", "queued")
state.db.close()
print("mixed temporary cursor rowcount passed")
