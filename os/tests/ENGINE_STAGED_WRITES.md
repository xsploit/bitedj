# Staged track/cue database writes

The import path needs one transaction covering track metadata, cue identities and source baselines. The existing live-object save path also changes cue IDs/dirty flags, emits signals and writes analysis/override files. These new APIs expose database-only operations:

- `TrackDAO::stageTrackRecord(transaction, record, beats, error)` updates one existing detached record using the same serializer as ordinary track saves. It rejects missing/ignored rows and does not commit, modify a live Track, write caches/files or publish model signals.
- `TrackDAO::stageNewTrackRecord(transaction, record, beats, fileInfo, insertedRecord, error)` inserts a file location and new library row, then serializes the detached record. It returns a copy with the allocated ID/date. Existing exact file locations are rejected for the caller to resolve/merge. A savepoint removes the file-location row if a later insert/update fails. The input record stays unassigned.
- `CueDAO::stageTrackCues(transaction, trackId, desiredCues, stagedCopies, error)` snapshots each input cue under its mutex, writes detached copies, removes omitted cues and returns copies carrying persisted IDs. Originals retain their IDs/dirty state. The caller must publish the returned objects only after commit and discard them on rollback. Input/output list aliasing is supported. Existing IDs must belong to the target track; duplicate IDs/control indexes are rejected. Missing SQL updates count as failure.

All methods require an active caller SqlTransaction. Cue staging also verifies that it belongs to the DAO's connection. Savepoints undo that operation's partial writes, while the caller controls the outer transaction. Callers should abort the entire import on staging failure. Source baseline writes can participate through EngineImportRegistry on the same connection.

Ordinary TrackDAO updates now call the common record serializer and check commit success before writing cue/rating override files. The ordinary CueDAO save API is otherwise unchanged; this does not claim to repair every legacy save/rollback behavior.

Tests use actual production code and SQLite:

```
python os/tests/test_engine_cue_origin.py BUILD --ninja NINJA --probe-source /absolute/repo/os/tests/engine_staged_cues_probe.cpp
python os/tests/test_engine_cue_origin.py BUILD --ninja NINJA --probe-source /absolute/repo/os/tests/engine_staged_record_probe.cpp --probe-arg /absolute/repo/res/schema.xml
```

For source-object diagnosis while a full build is still running, `--fresh-cues` compiles current Cue/CueDAO and `--fresh-trackdao` compiles TrackDAO separately. Such a pass is not a completed application-build pass. The precompiled header must already match its source headers.

The cue probe injects insert/delete failures, an ignored update and a deferred foreign-key commit failure. It checks savepoint/outer rollback, dirty-state/ID isolation, retry without duplicates, same-number source banks, fractional positions, orphan removal, foreign cue rejection, duplicate controls, inactive transaction rejection, aliased input/output and empty desired sets.

The record probe creates the actual schema0→41 with SchemaManager. It stages metadata/beats, a saved loop and its source baseline, then injects a final registry failure and verifies all prior writes roll back. It checks successful commit/retry, missing/rejected records, key/rating/beat serialization, location/date preservation, and new-record insertion/rollback/retry. A failed new-library insert must leave no file-location row; duplicate file insertion must leave row counts unchanged. Its small file is a file-info fixture, not decoded or playable audio.

Remaining integration duties are explicit: obtain fresh snapshots and detect concurrent live edits before publication; preserve/merge source cue and playlist identities; resolve canonical-path aliases and media/frame domains; coordinate GlobalTrackCache and model notifications after commit; move returned cue QObjects to their consumer thread before stopping a temporary worker; and expose the import/conflict/bank UI. The existing-record writer serializes a complete record, so callers cannot pass stale snapshots and assume unrelated live changes are protected. These tests do not establish full application import atomicity, real-time audio behavior or Windows support.

Completed Linux application validation also passed on a disposable profile and copied MP3. The real library model edited title, comment and BPM95→132; ordinary save and an actual restart exited0. The 29-byte BeatGrid2 blob changed with BPM and stayed identical across restart; all eight existing cue rows and their source origins stayed unchanged, with SQLite integrity OK. This covers ordinary save behavior, not an Engine import action.
