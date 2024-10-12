from xlang_sqlite import sqlite
sqlite.UseDatabase("../Config/session_memory")

def create_db():
    pushWritepad(sqlite)
    # Create a session_memory database with a composite primary key (session_id, timestamp)
    %session_if = SELECT name FROM sqlite_master WHERE type='table' AND name='session_memory';
    re = session_if.fetch()
    if re == None:
        %CREATE TABLE session_memory (\
            session_id TEXT,\
            content TEXT,\
            timestamp DATETIME DEFAULT CURRENT_TIMESTAMP,\
            role TEXT,\
            agent TEXT,\
            PRIMARY KEY (session_id, timestamp)\
        );
    popWritepad()

def query(session_id):
    pushWritepad(sqlite)
    # Query all records for a given session_id, ordered by timestamp
    %session_query = SELECT content, timestamp, role, agent FROM session_memory WHERE session_id = ${session_id} ORDER BY timestamp;
    results = []
    res = session_query.fetch()
    while res != None:
        results.append(res)
        res = session_query.fetch()
    popWritepad()
    return results

def add(session_id, content, role, agent):
    pushWritepad(sqlite)
    # Insert a new session record with session_id, content, role, and agent
    %BEGIN TRANSACTION;
    %INSERT INTO session_memory (session_id, content, role, agent) VALUES (${session_id}, ${content}, ${role}, ${agent});
    %COMMIT;
    popWritepad()
    return True

def remove_all(session_id):
    pushWritepad(sqlite)
    # Remove all records for the given session_id
    %DELETE FROM session_memory WHERE session_id = ${session_id};
    popWritepad()

def session_list():
    pushWritepad(sqlite)
    # Retrieve each session_id with the most recent timestamp
    %session_list_query = SELECT session_id, MAX(timestamp) as last_time FROM session_memory GROUP BY session_id;
    session_ids = []
    res = session_list_query.fetch()
    while res != None:
        session_ids.append({'id': res[0], 'time': res[1]})
        res = session_list_query.fetch()
    popWritepad()
    return session_ids

# Initialize the database
create_db()
