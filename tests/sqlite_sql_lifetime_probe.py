"""Reproduce native SQL pointer invalidation while binding long string parameters."""
import sqlite3

db = sqlite3.connect(":memory:")
db.execute("CREATE TABLE items(id TEXT PRIMARY KEY,status TEXT)")
for size in [24, 32, 36, 40, 48, 56, 64, 256]:
    long_id = "x" * size
    db.execute("INSERT INTO items VALUES (?,?)", (long_id, "queued"))
    cursor = db.execute("UPDATE items SET status=? WHERE id=? AND status=?", ("running", long_id, "queued"))
    count = cursor.rowcount
    actual = db.execute("SELECT status FROM items WHERE id=?", (long_id,)).fetchone()
    print("ID length", size, "UPDATE rowcount", count, "stored status", actual)
    assert actual[0] == "running"
    assert count == 1, "UPDATE rowcount must be 1 after binding text"
db.close()
