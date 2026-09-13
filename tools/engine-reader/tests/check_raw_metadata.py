"""Check SQL-origin metadata independently of libdjinterop's writer conversion."""
import json
from pathlib import Path
import sqlite3
import subprocess
import sys
import tempfile

generator, launcher = [str(Path(p).resolve()) for p in sys.argv[1:]]
for schema in ('3.0.0', '3.0.2'):
    with tempfile.TemporaryDirectory() as temp:
        library = Path(temp) / 'Engine Library'
        subprocess.run([generator, str(library)] +
                       (['3.0.0'] if schema == '3.0.0' else []), check=True)
        db = library / 'Database2' / 'm.db'
        with sqlite3.connect(db) as conn:
            # Give all tracks values injected directly into SQL. The reader must
            # not merely invert a coincidentally matching fixture-writer bug.
            for key in range(24):
                conn.execute('UPDATE Track SET key=?, rating=?, bitrate=?, '
                             'year=?, playOrder=?, fileBytes=?, comment=?, '
                             'composer=?, label=? WHERE id=?',
                             (key, (key % 5 + 1) * 20, 1536, 1999, key,
                              9007199254740993, 'Original / VIP', 'Raw composer',
                              'Raw publisher', key + 1))
            conn.execute('UPDATE Track SET title=NULL WHERE id=25')
            conn.execute("UPDATE Track SET title='' WHERE id=26")
        before = db.read_bytes()
        result = json.loads(subprocess.check_output(
            [launcher, str(library), '--media-root', temp], timeout=60))
        assert before == db.read_bytes(), 'Reader changed source database'
        tracks = {t['id']: t for t in result['tracks']}
        for key in range(24):
            t = tracks[str(key + 1)]
            assert t['keyId'] == key
            assert t['ratingPercent'] == (key % 5 + 1) * 20
            assert t['bitrateKbps'] == 1536
            assert t['year'] == 1999 and t['trackNumber'] == key
            assert t['fileBytes'] == '9007199254740993'
            assert t['comment'] == 'Original / VIP'
            assert t['composer'] == 'Raw composer'
            assert t['publisher'] == 'Raw publisher'
        # Legacy v1 title uses an empty display string for a missing SQL title.
        # This is deliberately documented as lossy; do not clear local titles.
        assert tracks['25']['title'] == ''
        assert tracks['25']['sourceTitle'] is None
        assert tracks['26']['sourceTitle'] == ''
        assert tracks['1']['sourceTitle'] == 'metadata-0'
        print(f'PASS: schema {schema}, direct SQL metadata, all keys/ratings, source unchanged')
