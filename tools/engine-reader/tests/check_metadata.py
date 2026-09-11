"""Run synthetic database -> installed launcher checks. Pass generator and launcher paths."""
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import copy

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from engine_reader_protocol import validate_tracks

generator, launcher = map(lambda s: str(Path(s).resolve()), sys.argv[1:])
fields = ('comment', 'composer', 'publisher', 'keyId', 'bitrateKbps',
          'ratingPercent', 'year', 'trackNumber', 'fileBytes')
with tempfile.TemporaryDirectory() as temp:
    library = Path(temp) / 'Engine Library'
    subprocess.run([generator, str(library)], check=True)
    result = json.loads(subprocess.check_output(
        [launcher, str(library), '--media-root', temp], timeout=60))
    tracks = {t['title']: t for t in result['tracks']}
    assert len(tracks) == 26
    for i in range(24):
        t = tracks[f'metadata-{i}']
        assert t['keyId'] == i
        assert t['bitrateKbps'] == 320
        assert t['ratingPercent'] == (None if i == 0 else 100)
        assert t['year'] == 2026 and t['trackNumber'] == i + 1
        assert t['fileBytes'] == '9007199254740993'
        assert t['comment'] == 'VIP — keep this version'
        assert t['composer'] == 'Composer' and t['publisher'] == 'Publisher'
    assert all(tracks['metadata-24'][f] is None for f in fields)
    zero = tracks['metadata-25']
    assert all(zero[f] == '' for f in ('comment', 'composer', 'publisher'))
    assert all(zero[f] == 0 for f in ('bitrateKbps', 'year', 'trackNumber'))
    assert zero['fileBytes'] == '0'
    assert zero['ratingPercent'] is None  # Engine sentinel 0 means unrated.
    assert all(t['media']['status'] == 'missing' for t in tracks.values())
    validate_tracks(result, result['sourceUuid'])
    for field, invalid in [('keyId', 24), ('keyId', True),
                           ('bitrateKbps', -1), ('ratingPercent', 101),
                           ('year', 2**31), ('trackNumber', 1.5),
                           ('fileBytes', '01'), ('fileBytes', str(2**64)),
                           ('fileBytes', 42), ('comment', 'bad\x00text'),
                           ('composer', False), ('publisher', 'x' * 65537)]:
        bad = copy.deepcopy(result)
        bad['tracks'][0][field] = invalid
        try:
            validate_tracks(bad, bad['sourceUuid'])
        except ValueError:
            pass
        else:
            raise AssertionError(f'Accepted invalid {field}')
print('PASS: 24 musical keys, nullable metadata, empty strings, zero values, exact large file bytes')
