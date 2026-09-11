import io,json,sqlite3,tempfile,unittest
from contextlib import closing
from pathlib import Path
from engine_limits import write_json_bounded
from engine_import_package import snapshot
from engine_playlist_manifest import manifest

class ImportLimits(unittest.TestCase):
    def test_output_boundary_no_partial_success(self):
        out=io.StringIO();write_json_bounded({},out,max_bytes=3);self.assertEqual(out.getvalue(),'{}\n')
        for obj,limit in [({},2),({'title':'x'*100},30),({'value':float('nan')},100)]:
            out=io.StringIO()
            with self.assertRaises(ValueError):write_json_bounded(obj,out,max_bytes=limit)
            self.assertEqual(out.getvalue(),'')
    def test_database_limit(self):
        with tempfile.TemporaryDirectory() as d:
            source=Path(d)/'source.db';dest=Path(d)/'snapshot.db'
            with closing(sqlite3.connect(source)) as c:c.execute('CREATE TABLE t(x)');c.commit()
            original=source.read_bytes()
            with self.assertRaises(ValueError):snapshot(source,dest,max_bytes=1)
            self.assertFalse(dest.exists());self.assertEqual(source.read_bytes(),original)
    def test_playlist_limits(self):
        with closing(sqlite3.connect(':memory:')) as c:
            c.executescript("CREATE TABLE Information(uuid,schemaVersionMajor,schemaVersionMinor,schemaVersionPatch);INSERT INTO Information VALUES('test',3,0,2);CREATE TABLE Playlist(id,title,parentListId,nextListId);INSERT INTO Playlist VALUES(1,'A',0,0);CREATE TABLE PlaylistEntity(id,listId,trackId,databaseUuid,nextEntityId);INSERT INTO PlaylistEntity VALUES(1,1,1,'test',0);")
            with self.assertRaises(ValueError):manifest(c,max_playlists=0)
            with self.assertRaises(ValueError):manifest(c,max_entries=0)
            self.assertEqual(len(manifest(c)['playlists']),1)

if __name__=='__main__':unittest.main()
