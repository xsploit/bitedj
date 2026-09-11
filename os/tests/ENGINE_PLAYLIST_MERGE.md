# Engine playlist re-import planning

`planEnginePlaylistImport` compares a source playlist with the last accepted
baseline and an ordered local snapshot. It plans a name and ordered occurrence
list without touching SQL, live Tracks, or playlist caches. A repeated track is
not a repeated occurrence: Engine entry IDs and local PlaylistTracks row IDs
keep each copy distinct through reorder, insertion, and removal.

The merge policy is field-wise (name and complete occurrence list):

- Local equals source: accept that baseline, retaining existing occurrence IDs.
- Local equals the previous baseline: accept the changed source value.
- Source equals the baseline: retain local edits, including additions, removals,
  renames, and reorders.
- Both changed differently: retain the local field, report a conflict, and keep
  its previous baseline. Repeating the import keeps reporting the conflict.

A source-only clear empties an unchanged playlist. Concurrent order/content edits
conflict as a whole; the planner does not guess how to interleave them. A name
change can still be accepted while contents conflict. An occurrence bound to a
locally deleted row uses a fresh row ID if restoration is eventually accepted.
Bindings must be scoped to the source library and playlist by the caller.

The caller resolves track identities, supplies an exact ordered row snapshot,
stages the plan with `PlaylistDAO::stagePlaylistUpdate`, and saves accepted
baselines and new occurrence bindings inside the same transaction. DAO checks
still reject stale, locked, hidden, or foreign snapshots. Save all track/cue and
registry writes before `commitStagedPlaylists`, which commits the whole transaction.
Do not use `PlaylistDAO::getTrackIds` for the snapshot: it uses DISTINCT.

## Verification

Against a completed Linux application build:

```sh
python3 os/tests/test_engine_cue_origin.py /path/to/build \
  --ninja /path/to/ninja \
  --probe-source "$PWD/os/tests/engine_playlist_merge_probe.cpp"
```

The probe links the actual application archive and creates an isolated in-memory
SQLite schema. It exercises planner → actual PlaylistDAO → import registry →
commit/rollback, including duplicate songs, unchanged re-import, source reorder/
delete/add, stable row identities, rollback and retry of both playlist and
occurrence provenance, local edits/deletion/reorder, persistent conflicts,
independent name/content decisions, malformed baselines, and invalid bindings.
It reads ordered database rows back and checks SQLite integrity.

This is not an end-to-end UI import test. Folder mapping, deletion of a whole
playlist absent from a new package, explicit conflict choices, live Track
publication, and the overall Apply coordinator remain unfinished. No Pi,
physical controller, native Engine timing, or touchscreen behavior is verified
by this test.

## Row-ID reuse and migration42

PlaylistTracks and Playlists use SQLite integer primary keys that can be reused
following deletion. A persisted ID alone is therefore not proof of occurrence
identity. Schema42 adds indexed deletion triggers that set the corresponding
entry/playlist registry `local_id` to NULL while retaining the source key and
baseline. Triggers participate in the deleting transaction, including rollback.
The first migration invalidates pre-trigger links, which could already be stale;
replaying the migration retains links when the corresponding trigger exists.
This intentionally requires old draft imports to be reviewed/rebound rather than
silently treating ambiguous row IDs as ownership.

Load fresh non-null bindings and exact local snapshots inside the Apply transaction;
a cached map may predate deletion. SchemaManager now supports an explicit SQL
statement separator per revision so complete trigger bodies are passed to SQLite.
Earlier revisions keep the default semicolon separator.

`engine_playlist_identity_probe.cpp` exercises the actual SchemaManager with the
production XML, upgrading an isolated version41 fixture to42. It forces reuse of
both occurrence and playlist IDs, checks transactional invalidation/rollback and
baseline preservation, and proves the planner retains the replacement local song
when the source deleted its former occurrence. Reapplying42 and SQLite integrity
also pass. Invoke through the completed-archive runner above, using this probe and
`--probe-arg "$PWD/res/schema.xml"`.
