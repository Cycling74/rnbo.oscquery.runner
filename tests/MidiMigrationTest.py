"""Exercise the actual SQL migration against rows written by a release runner."""
import json
from pathlib import Path
import re
import sqlite3

source = (Path(__file__).resolve().parents[1] / "src/DB.cpp").read_text()


def statements(version):
    body = source.split(f"do_migration({version},", 1)[1].split("\n  });", 1)[0]
    found = re.findall(r'db.exec\((?:R"\((.*?)\)"|"((?:\\.|[^"\\])*)")\);', body, re.S)
    return [raw or json.loads('"' + quoted + '"') for raw, quoted in found]


assert "do_migration(25," not in source, "MIDI storage is a single migration"

db = sqlite3.connect(":memory:")
db.execute("PRAGMA foreign_keys = ON")
db.execute("CREATE TABLE sets (id INTEGER PRIMARY KEY)")
db.execute("INSERT INTO sets VALUES (1)")
# Use the original table definition, including its unique key and foreign key.
create = next(sql for sql in statements(12) if re.search(r'CREATE TABLE sets_connections\s*\(', sql))
db.execute(create)
db.execute("INSERT INTO sets_connections (id,set_id,source_name,source_instance_index,source_port_name,sink_name,sink_instance_index,sink_port_name) "
           "VALUES (1,1,'in-hw-3-0-0-Keys',-1,'','synth-0',0,'midiin1')")

sql = statements(24)
assert len(sql) == 4, "Update SQL extraction for migration 24"
for statement in sql:
    db.execute(statement)

columns = {row[1] for row in db.execute("PRAGMA table_info(sets_connections)")}
assert {'source_midi_identity', 'sink_midi_identity', 'source_midi_port', 'sink_midi_port'} <= columns
assert db.execute("SELECT count(*) FROM sqlite_master WHERE name='sets_connections_midi'").fetchone()[0] == 0
select = ("SELECT source_name,source_instance_index,source_port_name,sink_name,sink_instance_index,sink_port_name,"
          "source_midi_identity,sink_midi_identity,source_midi_port,sink_midi_port FROM sets_connections WHERE id=?")
assert db.execute(select, (1,)).fetchone() == ('in-hw-3-0-0-Keys', -1, '', 'synth-0', 0, 'midiin1', '', '', '', ''), \
    "a release row keeps its id and address and gets empty MIDI fields"

# The statement the runner saves with.
insert = re.search(r'(INSERT INTO sets_connections\s*\(set_id.*?VALUES\s*\(.*?\))', source, re.S).group(1)
insert = re.sub(r'\?\d+', '?', insert)
row = (1, 'Twin', -1, 'Keys (out)', 'synth-0', 0, 'midiin1', 'name:twins:source', '', 'system:midi_capture_1', '')
db.execute(insert, row)
# An identical device with the same alias address and identity keeps its own cable: the hint differs.
db.execute(insert, row[:9] + ('system:midi_capture_2', ''))
assert db.execute("SELECT source_midi_port FROM sets_connections WHERE source_name='Twin' ORDER BY id").fetchall() == \
    [('system:midi_capture_1',), ('system:midi_capture_2',)]
# The same cable twice is still a duplicate.
try:
    db.execute(insert, row)
except sqlite3.IntegrityError:
    pass
else:
    raise AssertionError('Unique cable constraint was lost')
assert db.execute('PRAGMA foreign_key_check').fetchall() == []
db.execute('DELETE FROM sets WHERE id=1')
assert db.execute('SELECT count(*) FROM sets_connections').fetchone()[0] == 0
print('MIDI migration keeps release rows and ids, twin cables, the unique key and foreign keys')
