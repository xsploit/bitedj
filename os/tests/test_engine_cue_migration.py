"""Exercise the shipped schema migration against an existing cue table."""
from pathlib import Path
import sqlite3
import xml.etree.ElementTree as ET
root=Path(__file__).resolve().parents[2]
schema=ET.parse(root/'res/schema.xml').getroot()
revision=next(r for r in schema.findall('revision') if r.attrib['version']=='40')
with sqlite3.connect(':memory:') as db:
    db.execute('CREATE TABLE cues(id INTEGER PRIMARY KEY, track_id INTEGER, type INTEGER, position REAL, length REAL, hotcue INTEGER, label TEXT, color INTEGER)')
    row=(1,7,4,882000.5,176400.5,26,'Local loop',0x123456)
    db.execute('INSERT INTO cues VALUES(?,?,?,?,?,?,?,?)',row)
    db.executescript(revision.findtext('sql'))
    actual=db.execute('SELECT * FROM cues').fetchone()
    assert actual[:8]==row and actual[8:]==(None,None,None,None)
    db.execute('UPDATE cues SET engine_library_uuid=?,engine_track_id=?,engine_bank=?,engine_slot=? WHERE id=1',('fixture-library','9223372036854775807',2,1))
    assert db.execute('SELECT engine_track_id FROM cues').fetchone()[0]=='9223372036854775807'
print('PASS: schema40 preserves existing slot26 cue, nullable origin and exact source track ID')
