"""Cancel the real launcher while an instrumented adjacent reader is running."""
from pathlib import Path
import json,os,shutil,signal,sqlite3,subprocess,sys,tempfile,time,unittest
package=Path(__file__).resolve().parents[1]

def running(pid):
    try:
        return Path(f'/proc/{pid}/stat').read_text().split(') ',1)[1][0] != 'Z'
    except FileNotFoundError:
        return False

class Cancellation(unittest.TestCase):
    def test_term_and_interrupt_clean_child_and_snapshot(self):
        for sig in (signal.SIGTERM,signal.SIGINT):
            with self.subTest(signal=sig),tempfile.TemporaryDirectory() as temp:
                root=Path(temp);stage=root/'stage';stage.mkdir()
                for source in package.glob('*.py'):shutil.copy2(source,stage/source.name)
                marker=root/'started.json'
                helper=stage/'engine-reader'
                helper.write_text('#!/usr/bin/env python3\nimport os,sys,json,time\nfrom pathlib import Path\n'+
                    f'Path({str(marker)!r}).write_text(json.dumps({{"pid":os.getpid(),"snapshot":sys.argv[1]}}))\n'+
                    'while True: time.sleep(1)\n')
                helper.chmod(0o700)
                library=root/'Engine Library';(library/'Database2').mkdir(parents=True)
                with sqlite3.connect(library/'Database2/m.db') as c:
                    c.executescript('''CREATE TABLE Information(uuid,schemaVersionMajor,schemaVersionMinor,schemaVersionPatch);
                    INSERT INTO Information VALUES('synthetic',3,0,2);
                    CREATE TABLE Playlist(id,title,parentListId,nextListId);
                    CREATE TABLE PlaylistEntity(id,listId,trackId,databaseUuid,nextEntityId);''')
                child=None
                p=subprocess.Popen([sys.executable,str(stage/'engine-import.py'),str(library)],stdout=subprocess.PIPE,stderr=subprocess.PIPE)
                try:
                    deadline=time.monotonic()+5
                    while not marker.exists() and p.poll() is None and time.monotonic()<deadline:time.sleep(.01)
                    self.assertTrue(marker.exists(),'Reader never started')
                    evidence=json.loads(marker.read_text());child=evidence['pid']
                    self.assertTrue(running(child))
                    p.send_signal(sig);out,err=p.communicate(timeout=5)
                    self.assertNotEqual(p.returncode,0)
                    self.assertEqual(out,b'','Cancelled import emitted success output')
                    self.assertFalse(running(child),'Reader survived launcher cancellation')
                    self.assertFalse(Path(evidence['snapshot']).exists(),'Snapshot leaked after cancellation')
                finally:
                    if p.poll() is None:p.kill();p.communicate()
                    if child and running(child):os.kill(child,signal.SIGKILL)

if __name__=='__main__':unittest.main()
