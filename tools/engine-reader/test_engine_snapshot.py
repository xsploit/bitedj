"""Verify SQLite backup includes committed WAL without changing source files."""
from contextlib import closing
import hashlib
from pathlib import Path
import sqlite3
import tempfile
import unittest
from engine_import_package import snapshot

class Snapshot(unittest.TestCase):
    def test_committed_wal_included_without_source_writes(self):
        with tempfile.TemporaryDirectory() as temp:
            source=Path(temp)/'source.db'; target=Path(temp)/'snapshot.db'
            with closing(sqlite3.connect(source)) as writer:
                writer.execute('CREATE TABLE Track(id INTEGER PRIMARY KEY, title TEXT)')
                writer.execute("INSERT INTO Track VALUES(1,'Original')")
                writer.commit()
                writer.execute('PRAGMA journal_mode=WAL')
                writer.execute('PRAGMA wal_autocheckpoint=0')
                writer.execute("UPDATE Track SET title='Committed WAL' WHERE id=1")
                writer.commit()
                wal=Path(str(source)+'-wal');self.assertGreater(wal.stat().st_size,0)
                before=[hashlib.sha256(p.read_bytes()).hexdigest() for p in (source,wal)]
                snapshot(source,target)
                with closing(sqlite3.connect(target)) as reader:
                    self.assertEqual(reader.execute('SELECT title FROM Track').fetchone()[0],'Committed WAL')
                self.assertEqual(before,[hashlib.sha256(p.read_bytes()).hexdigest() for p in (source,wal)])

if __name__=='__main__':unittest.main()
