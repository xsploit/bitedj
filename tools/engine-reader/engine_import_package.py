"""Produce an isolated Engine import document from a consistent SQLite backup."""
import argparse
from contextlib import closing
import hashlib
import json
from pathlib import Path
import sqlite3
import tempfile
import time
from engine_playlist_manifest import manifest
from engine_reader_protocol import read_limited, validate_tracks
from engine_media_path import resolve_media


def snapshot(source, target, max_bytes=2*1024**3):
    deadline = time.monotonic() + 30
    page_size = 4096
    def progress(status, remaining, total):
        if total * page_size > max_bytes:
            raise ValueError('Engine database exceeds snapshot size limit')
        if time.monotonic() > deadline:
            raise TimeoutError('Engine database snapshot exceeded 30 seconds')
    with closing(sqlite3.connect(source.resolve().as_uri()+'?mode=ro', uri=True, timeout=5)) as src:
        src.execute('PRAGMA query_only=ON')
        page_size = src.execute('PRAGMA page_size').fetchone()[0]
        if src.execute('PRAGMA page_count').fetchone()[0] * page_size > max_bytes:
            raise ValueError('Engine database exceeds snapshot size limit')
        with closing(sqlite3.connect(target)) as dst:
            src.backup(dst,pages=256,progress=progress,sleep=0.05)


def package(source, reader, media_root=None):
    with tempfile.TemporaryDirectory(prefix='bitedj-engine-import-') as temp:
        library=Path(temp)/'Engine Library'
        db=library/'Database2'/'m.db'
        db.parent.mkdir(parents=True)
        snapshot(source/'Database2'/'m.db', db)
        hasher=hashlib.sha256()
        with db.open('rb') as frozen:
            for chunk in iter(lambda:frozen.read(1024*1024),b''):
                hasher.update(chunk)
        digest=hasher.hexdigest()
        with closing(sqlite3.connect(db.as_uri()+'?mode=ro',uri=True)) as conn:
            conn.execute('BEGIN')
            playlists=manifest(conn)
        tracks=read_limited(reader,library)
        track_ids=validate_tracks(tracks,playlists['sourceUuid'])
        if media_root is not None:
            for track in tracks['tracks']:
                track['media']=resolve_media(source,media_root,track['relativePath'])
        for playlist in playlists['playlists']:
            for entry in playlist['tracks']:
                local=entry['sourceUuid']==playlists['sourceUuid']
                entry['resolution']='local' if local and entry['trackId'] in track_ids else 'unresolved'
        return {'protocol':'bitedj.engine.import','protocolVersion':1,
                'sourceUuid':playlists['sourceUuid'],'schema':playlists['schema'],
                'snapshotSha256':digest,
                'mediaPathContext': {'libraryDirectory':str(source.resolve()),
                                     'relativePathBase':'original Engine Library directory',
                                     'resolvePolicy':'preserve reference; validate resolved media before opening'},
                'frameUnit':'audio frames at track sample rate',
                'tracks':tracks['tracks'],'playlists':playlists['playlists']}


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('source',type=Path)
    p.add_argument('reader',type=Path)
    p.add_argument('--media-root',type=Path,help='Explicit original media root for resolving track paths')
    args=p.parse_args()
    print(json.dumps(package(args.source,args.reader,args.media_root),indent=2,allow_nan=False))

if __name__=='__main__': main()
