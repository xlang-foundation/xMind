import _sqlite3
connection = _sqlite3.connect('C:/Users/shaw9/AppData/Local/Temp/xmind-model-rpc-683303613-3303948748/state.sqlite')
print(connection.execute("SELECT id,state,parent_run_id FROM runs ORDER BY rowid DESC LIMIT 7").fetchall())
print(connection.execute("SELECT run_id,kind FROM events ORDER BY seq DESC LIMIT 8").fetchall())
