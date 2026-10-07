"""Check temporary cursor rowcount vs retained cursor on xlang3."""
import sqlite3

db = sqlite3.connect(":memory:")
db.execute("CREATE TABLE runs(id TEXT PRIMARY KEY,status TEXT)")
db.execute("INSERT INTO runs VALUES ('first','queued')")
for index in range(20):
    with db:
        count = db.execute("UPDATE runs SET status=? WHERE id=? AND status=?", ("running", "first", "queued")).rowcount
        print("temporary", index, count, db.execute("SELECT status FROM runs").fetchone())
        assert count == 1
        cursor = db.execute("UPDATE runs SET status=? WHERE id=? AND status=?", ("queued", "first", "running"))
        print("retained", index, cursor.rowcount)
        assert cursor.rowcount == 1
db.close()
print("sqlite rowcount probe passed")
