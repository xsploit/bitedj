"""Read Engine playlist identity/order in one read-only SQLite transaction.

Prototype for the isolated import protocol, not a BiteDJ integration.
"""
import argparse
from collections import defaultdict
from contextlib import closing
import json
from pathlib import Path
import sqlite3
import time


def ordered(rows, link):
    """Resolve Engine's next-ID chain, rejecting ambiguous or disconnected data."""
    if not rows:
        return []
    by_next = {}
    ids = set()
    for row in rows:
        if row[link] in by_next or row['id'] in ids or row['id'] == 0:
            raise ValueError('Ambiguous playlist order')
        by_next[row[link]] = row
        ids.add(row['id'])
    result, seen, cursor = [], set(), 0
    while cursor in by_next:
        row = by_next[cursor]
        if row['id'] in seen:
            raise ValueError('Cyclic playlist order')
        seen.add(row['id'])
        result.append(row)
        cursor = row['id']
    if len(result) != len(rows):
        raise ValueError('Disconnected or cyclic playlist order')
    return result[::-1]


def manifest(connection, max_playlists=100000, max_entries=1000000):
    deadline=time.monotonic()+15
    connection.set_progress_handler(lambda:int(time.monotonic()>deadline),1000)
    connection.setlimit(sqlite3.SQLITE_LIMIT_LENGTH,1024*1024)
    connection.row_factory = sqlite3.Row
    info = connection.execute('SELECT uuid, schemaVersionMajor, schemaVersionMinor, schemaVersionPatch FROM Information LIMIT 2').fetchall()
    if len(info) != 1 or not info[0]['uuid']:
        raise ValueError('Expected one identified Engine library')
    uuid = info[0]['uuid']
    version = tuple(info[0][k] for k in ('schemaVersionMajor','schemaVersionMinor','schemaVersionPatch'))
    if version not in ((3,0,0),(3,0,2)):
        raise ValueError('Playlist schema not yet validated: '+str(version))
    rows = [dict(r) for r in connection.execute('SELECT id, title, parentListId, nextListId FROM Playlist LIMIT ?', (max_playlists+1,))]
    if len(rows)>max_playlists:
        raise ValueError('Playlist count exceeds import limit')
    for row in rows:
        if not isinstance(row['title'],str) or len(row['title'])>65536 or '\x00' in row['title']:
            raise ValueError('Invalid playlist title')
    entities = defaultdict(list)
    for index,row in enumerate(connection.execute('SELECT id, listId, trackId, databaseUuid, nextEntityId FROM PlaylistEntity LIMIT ?', (max_entries+1,))):
        if index>=max_entries:
            raise ValueError('Playlist entry count exceeds import limit')
        entities[row['listId']].append(dict(row))
    ids = {r['id'] for r in rows}
    if set(entities) - ids:
        raise ValueError('Playlist entries reference a missing playlist')
    groups = defaultdict(list)
    for row in rows:
        parent = row['parentListId']
        if parent != 0 and parent not in ids:
            raise ValueError('Missing playlist parent')
        groups[parent].append(row)
    children = {p: ordered(rs, 'nextListId') for p,rs in groups.items()}
    result, seen = [], set()
    stack = [(r, None, n) for n,r in reversed(list(enumerate(children.get(0, []))))]
    while stack:
        row, parent, position = stack.pop()
        identity = row['id']
        if identity in seen:
            raise ValueError('Cyclic playlist hierarchy')
        seen.add(identity)
        entries = ordered(entities[identity], 'nextEntityId')
        result.append({'id': str(identity), 'parentId': parent, 'position': position,
                       'title': row['title'], 'tracks': [
                           {'entryId': str(e['id']), 'trackId': str(e['trackId']),
                            'sourceUuid': e['databaseUuid']} for e in entries]})
        stack.extend((r,str(identity),n) for n,r in reversed(list(enumerate(children.get(identity, [])))))
    if seen != ids:
        raise ValueError('Unreachable or cyclic playlist hierarchy')
    return {'protocol': 'bitedj.engine.playlists', 'protocolVersion': 1,
            'sourceUuid': uuid, 'schema': '.'.join(map(str,version)), 'playlists': result}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('database',type=Path)
    args = parser.parse_args()
    with closing(sqlite3.connect(args.database.resolve().as_uri()+'?mode=ro',uri=True,timeout=5)) as conn:
        conn.execute('PRAGMA query_only=ON')
        conn.execute('BEGIN')
        print(json.dumps(manifest(conn),indent=2))

if __name__ == '__main__':
    main()
