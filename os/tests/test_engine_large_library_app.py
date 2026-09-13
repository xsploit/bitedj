#!/usr/bin/env python3
"""Measure real Engine preview/Apply with a larger disposable synthetic library.

No real profile, drive or music is used. This measures desktop GUI heartbeat and
process RSS, not Pi performance or simultaneous audio playback.
"""
from pathlib import Path
import argparse, hashlib, json, os, shlex, sqlite3, subprocess, tempfile, time, wave
p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--build', type=Path, required=True)
p.add_argument('--reader-prefix', type=Path, required=True)
p.add_argument('--fixture-generator', type=Path, required=True)
p.add_argument('--output', type=Path, required=True)
p.add_argument('--tracks', type=int, default=1000)
p.add_argument('--schema', choices=['3.0.0', '3.0.2'], default='3.0.2')
a = p.parse_args()
if not 2 <= a.tracks <= 10000: p.error('--tracks must be 2..10000')
repo = Path(__file__).resolve().parents[2]
out = a.output.resolve(); out.mkdir(parents=True, exist_ok=True)
binary = a.build.resolve() / 'mixxx'
helper = a.reader_prefix.resolve() / 'libexec/bitedj-engine/engine-import.py'
with tempfile.TemporaryDirectory(prefix='bitedj-large-library-') as td:
    root = Path(td); drive = root/'drive'; drive.mkdir()
    library = drive/'Engine Library'; music = drive/'Music'; music.mkdir()
    subprocess.run([str(a.fixture_generator.resolve()), str(library), 'initial', a.schema], check=True)
    wav = music/'Signal Original.wav'
    with wave.open(str(wav), 'wb') as f:
        f.setnchannels(2); f.setsampwidth(2); f.setframerate(44100); f.writeframes(bytes(132300*4))
    os.link(wav, music/'Signal VIP.wav')
    source = library/'Database2/m.db'
    with sqlite3.connect(source) as db:
        db.row_factory = sqlite3.Row
        track = dict(db.execute('SELECT * FROM Track WHERE id=1').fetchone())
        perf = dict(db.execute('SELECT * FROM PerformanceData WHERE trackId=1').fetchone())
        entity = dict(db.execute('SELECT * FROM PlaylistEntity WHERE listId=1 LIMIT 1').fetchone())
        def insert(table, row):
            names = ','.join('"'+k+'"' for k in row)
            db.execute(f'INSERT INTO "{table}" ({names}) VALUES ({",".join("?" for _ in row)})', list(row.values()))
        for i in range(3, a.tracks+1):
            filename = f'Stress_{i:05d}.wav'; os.link(wav, music/filename)
            row = dict(track, id=i, path='../Music/'+filename, filename=filename, title=f'Stress {i:05d}', originTrackId=i)
            insert('Track', row)
            # The source schema creates PerformanceData through an insert trigger.
            fields = [k for k in perf if k != 'trackId']
            db.execute('UPDATE PerformanceData SET '+','.join('"'+k+'"=?' for k in fields)+' WHERE trackId=?',
                [perf[k] for k in fields]+[i])
        # One complete ordered list; a second existing child retains track 2.
        db.execute('DELETE FROM PlaylistEntity WHERE listId=1')
        for i in range(1, a.tracks+1):
            insert('PlaylistEntity', dict(entity, id=i+10000, trackId=i, nextEntityId=i+10001 if i<a.tracks else 0))
        assert db.execute('PRAGMA integrity_check').fetchone()[0] == 'ok'
    source_hash = hashlib.sha256(source.read_bytes()).hexdigest()
    audio_hash = hashlib.sha256(wav.read_bytes()).hexdigest()
    flags = shlex.split(subprocess.check_output(['pkg-config','--cflags','--libs','Qt6Widgets'], text=True))
    driver = root/'driver.so'
    subprocess.run(['c++','-std=c++20','-fPIC','-shared',str(repo/'os/tests/engine_large_library_app_driver.cpp'),'-o',str(driver),*flags],check=True)
    settings = root/'profile'; settings.mkdir(); (settings/'mixxx.cfg').write_text('[Config]\nVersion 2.5.6\n')
    env = dict(os.environ, QT_QPA_PLATFORM='offscreen', LANG='C.UTF-8', LD_PRELOAD=str(driver),
        BITEDJ_ENGINE_IMPORT_HELPER=str(helper), BITEDJ_LARGE_DRIVE=str(drive),
        BITEDJ_LARGE_COUNT=str(a.tracks), BITEDJ_LARGE_RESULT=str(out/'timings.json'))
    peak_rss_kib = 0; started = time.monotonic()
    with (out/'application.log').open('w') as log:
        proc = subprocess.Popen([str(binary),'--settings-path',str(settings),'--resource-path',str(repo/'res')],env=env,stdout=log,stderr=subprocess.STDOUT)
        try:
            while proc.poll() is None:
                try:
                    values = Path(f'/proc/{proc.pid}/status').read_text().splitlines()
                    peak_rss_kib = max(peak_rss_kib, next(int(v.split()[1]) for v in values if v.startswith('VmRSS:')))
                except (FileNotFoundError, ProcessLookupError, StopIteration): pass
                if time.monotonic()-started > 270: raise TimeoutError('app timeout')
                time.sleep(.05)
        finally:
            if proc.poll() is None: proc.kill()
            proc.wait()
    log = (out/'application.log').read_text(errors='replace')
    assert proc.returncode == 0 and 'LARGE_TEST PASS' in log, (proc.returncode, log[-3000:])
    assert source_hash == hashlib.sha256(source.read_bytes()).hexdigest()
    assert audio_hash == hashlib.sha256(wav.read_bytes()).hexdigest()
    with sqlite3.connect(settings/'mixxxdb.sqlite') as db:
        assert db.execute('SELECT count(*) FROM library').fetchone()[0] == a.tracks
        playlist = db.execute("SELECT id FROM Playlists WHERE name='Prepared set' AND hidden=0").fetchone()[0]
        titles = [x[0] for x in db.execute('SELECT l.title FROM PlaylistTracks p JOIN library l ON l.id=p.track_id WHERE p.playlist_id=? ORDER BY p.position,p.id',(playlist,))]
        assert titles == ['Signal (Original)','Signal (VIP)']+[f'Stress {i:05d}' for i in range(3,a.tracks+1)]
        assert db.execute("SELECT count(*) FROM engine_import_entities WHERE entity_kind='track'").fetchone()[0] == a.tracks
        assert db.execute('PRAGMA integrity_check').fetchone()[0] == 'ok'
    result = json.loads((out/'timings.json').read_text())
    result.update(tracks=a.tracks, sourceSchema=a.schema, sampledPeakAppRssKiB=peak_rss_kib, sourceUnchanged=True,
        mediaUnchanged=True, orderedPlaylistVerified=True, integrity='ok', binarySHA256=hashlib.sha256(binary.read_bytes()).hexdigest(),
        limitations='Synthetic hardlinked WAVs, desktop offscreen; RSS excludes reader child; no playback, Pi or full-library bound.')
    (out/'results.json').write_text(json.dumps(result,indent=2)+'\n'); print(json.dumps(result,indent=2))
