import sqlite3
import unittest
from engine_playlist_manifest import manifest, ordered

class Playlists(unittest.TestCase):
    def database(self):
        c=sqlite3.connect(':memory:')
        self.addCleanup(c.close)
        c.executescript('''CREATE TABLE Information(uuid, schemaVersionMajor, schemaVersionMinor, schemaVersionPatch);
        INSERT INTO Information VALUES('local',3,0,2);
        CREATE TABLE Playlist(id,title,parentListId,nextListId);
        CREATE TABLE PlaylistEntity(id,listId,trackId,databaseUuid,nextEntityId);
        INSERT INTO Playlist VALUES(10,'Second',0,0),(20,'First',0,10),(30,'Nested',20,0);
        INSERT INTO PlaylistEntity VALUES(100,20,7,'local',0),(200,20,9,'external',100),(300,20,7,'local',200);''')
        return c
    def test_order_identity_and_duplicate_membership(self):
        r=manifest(self.database())
        self.assertEqual([p['id'] for p in r['playlists']],['20','30','10'])
        self.assertEqual([p['position'] for p in r['playlists']],[0,0,1])
        self.assertEqual(r['playlists'][1]['parentId'],'20')
        entries=r['playlists'][0]['tracks']
        self.assertEqual([e['entryId'] for e in entries],['300','200','100'])
        self.assertEqual([e['trackId'] for e in entries],['7','9','7'])
        self.assertEqual(entries[1]['sourceUuid'],'external')
    def test_invalid_graphs(self):
        for sql in [
            'UPDATE Playlist SET parentListId=30 WHERE id=20',
            'UPDATE Playlist SET parentListId=999 WHERE id=20',
            'UPDATE Playlist SET nextListId=20 WHERE id=10',
            'UPDATE Playlist SET nextListId=0 WHERE id=20',
            'UPDATE PlaylistEntity SET nextEntityId=300 WHERE id=100',
            'UPDATE PlaylistEntity SET nextEntityId=999 WHERE id=100',
            'UPDATE PlaylistEntity SET listId=999 WHERE id=100']:
            with self.subTest(sql=sql):
                c=self.database(); c.execute(sql)
                with self.assertRaises(ValueError): manifest(c)
    def test_empty(self):
        c=self.database(); c.execute('DELETE FROM PlaylistEntity'); c.execute('DELETE FROM Playlist')
        self.assertEqual(manifest(c)['playlists'],[])
    def test_wal_transaction_consistency(self):
        import tempfile
        from pathlib import Path
        with tempfile.TemporaryDirectory() as d:
            p=Path(d)/'library.db'; writer=sqlite3.connect(p)
            self.database().backup(writer)
            writer.execute('PRAGMA journal_mode=WAL')
            reader=sqlite3.connect(p.as_uri()+'?mode=ro',uri=True)
            reader.execute('BEGIN')
            original=manifest(reader)
            writer.execute("UPDATE Playlist SET title='Committed in WAL' WHERE id=20"); writer.commit()
            self.assertEqual(manifest(reader),original)
            reader.rollback()
            self.assertEqual(manifest(reader)['playlists'][0]['title'],'Committed in WAL')
            reader.close(); writer.close()

if __name__=='__main__': unittest.main()
