import sys
if len(sys.argv) > 1 and sys.argv[1]:
    sys.path.insert(0, sys.argv[1])
import sqlite3

def rejects(kind, action):
    try:
        action()
    except kind:
        return
    raise AssertionError("expected rejection")

for level in (None, "", "DEFERRED", "immediate", "exclusive"):
    db = sqlite3.connect(":memory:", isolation_level=level)
    normalized = level.upper() if isinstance(level, str) else None
    assert db.isolation_level == normalized
    assert db.in_transaction is False
    db.execute("CREATE TABLE data(id INTEGER PRIMARY KEY, text TEXT, blob BLOB)")
    text = "hello\x00world"
    cursor = db.execute("INSERT INTO data VALUES(?,?,?)", (1, text, b"\x00\xff"))
    assert cursor.rowcount == 1
    assert cursor.lastrowid == 1
    assert db.in_transaction is (level is not None)
    row = db.execute("SELECT text,blob FROM data").fetchone()
    assert len(row) == 2
    assert row[0] == text
    assert row[1] == b"\x00\xff"
    if level is not None:
        db.rollback()
        assert db.execute("SELECT count(*) FROM data").fetchone()[0] == 0
    else:
        db.execute("BEGIN IMMEDIATE")
        cursor = db.execute("UPDATE data SET text=? WHERE id=?", ("other\x00value", 1))
        assert cursor.rowcount == 1
        assert db.in_transaction
        db.rollback()
        assert db.execute("SELECT text FROM data").fetchone()[0] == text
    db.isolation_level = "immediate"
    assert db.isolation_level == "IMMEDIATE"
    db.execute("INSERT INTO data VALUES(?,?,?)", (2, "pending", b""))
    assert db.in_transaction
    db.isolation_level = None
    assert db.isolation_level is None
    assert db.in_transaction is False
    assert db.execute("SELECT text FROM data WHERE id=2").fetchone()[0] == "pending"
    rejects(ValueError, lambda: setattr(db, "isolation_level", "invalid"))
    assert db.isolation_level is None
    rejects(TypeError, lambda: setattr(db, "isolation_level", 42))
    db.close()
    rejects(sqlite3.ProgrammingError, lambda: getattr(db, "isolation_level"))

for invalid in (42, b"IMMEDIATE", [], {}):
    rejects(TypeError, lambda: sqlite3.connect(":memory:", isolation_level=invalid))
for invalid in ("invalid", "immediate\x00", " deferred"):
    rejects(ValueError, lambda: sqlite3.connect(":memory:", isolation_level=invalid))

default = sqlite3.connect(":memory:")
assert default.isolation_level == ""
default.close()
print("sqlite isolation and binary text contracts passed")
